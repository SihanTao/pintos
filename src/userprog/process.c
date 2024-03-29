#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <list.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define check_valid_push_stack(source, size)                                  \
  do                                                                          \
    {                                                                         \
      if (available_space >= (size))                                          \
        {                                                                     \
          available_space -= (size);                                          \
          *esp -= (size);                                                     \
          memcpy (*esp, (source), (size));                                    \
        }                                                                     \
      else                                                                    \
        {                                                                     \
          success = false;                                                    \
          goto done;                                                          \
        }                                                                     \
    }                                                                         \
  while (0)
// we don't need to free the kpage, since it is installed in thread pagedir,
// so that it is freed once we call thread_exit

#define push_stack(source) check_valid_push_stack (&(source), sizeof (source))
#define word_align(value) ((unsigned int)(value)&0xfffffffc)

#define MAX_FILENAME_LEN 14
#define NOT_INITIALIZE NULL

/* A structure to store the state of child process */

static struct start_process_args
{
  struct semaphore child_setup_sema; // Used by parent process to wait for
                                     // child to finish loading
  bool child_start_success; // To indicate whether child process has load
                            // successfully
  struct process_child_state *state;      // initialized in child process
  char thread_name[MAX_FILENAME_LEN + 1]; // initialized in parent process
  void *temp_for_build_stack;             // temp page to store the command line   
  int argc;                               // number of args
  size_t len_argv;                        // length of command line 
};

static thread_func start_process NO_RETURN;
static bool load (void *process_args, void (**eip) (void), void **esp);
static struct process_child_state *pids_find_and_remove (struct list *l,
                                                         pid_t pid);
static void free_file_descriptors (struct thread *t);

static void free_list_of_children (struct thread *t);

static void free_start_process_args (struct start_process_args *start_process_args);
static struct start_process_args *init_start_process_args (void);

void
free_start_process_args (struct start_process_args *start_process_args)
{
  palloc_free_page (start_process_args->temp_for_build_stack);
  free (start_process_args);
}

// this is only called in parent process
// it initialize everything that can be initialized inside the parent process
// it doesn't initialize child_state
// child_state is only initialize after everything is correct
static struct start_process_args *
init_start_process_args (void)
{
  struct start_process_args *start_process_args
      = malloc (sizeof (struct start_process_args));
  if (!start_process_args)
    return NULL;

  start_process_args->temp_for_build_stack = palloc_get_page (0);
  if (!start_process_args->temp_for_build_stack)
    {
      free (start_process_args);
      return NULL;
    }

  sema_init (&start_process_args->child_setup_sema, 0);
  start_process_args->child_start_success = false;

  return start_process_args;
}

// this is only called in child process
// only initialize process_child_state after everything is successful

// so that only child thread need to free it once an error happen
static struct process_child_state *
init_child_state (void)
{
  struct process_child_state *child_state
      = malloc (sizeof (struct process_child_state));
  if (!child_state)
    return NULL;

  child_state->pid = thread_current ()->tid;
  child_state->parent_exited = false;
  child_state->child_exited = false;
  child_state->exit_status = -1;
  sema_init (&child_state->wait_sema, 0);
  lock_init (&child_state->lock);

  return child_state;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  tid_t tid;
  struct start_process_args *process_args = init_start_process_args ();
  // this stucture is initialized in parent process
  // and only freed in parent process
  // you should not see this is freed in anywhere else

  if (process_args == NULL)
    return TID_ERROR;

  char *save_ptr;
  strlcpy (process_args->thread_name, file_name, MAX_FILENAME_LEN + 1);
  strtok_r (process_args->thread_name, " ", &save_ptr);
  int len_argv = 0;
  int argc = 1;
  bool previous_is_space = true;
  for (int i = 0;; i++)
    {
      if (i == PGSIZE) // the command line is too long
        {
          free_start_process_args (process_args);
          return TID_ERROR;
        }

      if (file_name[i] == ' '
          && previous_is_space) // ignore initial and consecutive spaces
        continue;

      if (file_name[i] == '\0')
        {
          ((char *)process_args->temp_for_build_stack)[len_argv] = '\0';
          len_argv++;
          break;
        }

      if (file_name[i] == ' ' && !previous_is_space)
        {
          previous_is_space = true;
          ((char *)process_args->temp_for_build_stack)[len_argv] = '\0';
          argc++;
        }
      else
        {
          previous_is_space = false;
          ((char *)process_args->temp_for_build_stack)[len_argv]
              = file_name[i];
        }
      len_argv++;
    }
  process_args->len_argv = len_argv;
  process_args->argc = argc;

  tid = thread_create (process_args->thread_name, PRI_DEFAULT, start_process,
                       process_args);
  // child process is not created at all
  // there is no thread call sema_up, thus return earlier
  if (tid == TID_ERROR)
    {
      free_start_process_args (process_args);
      return TID_ERROR;
    }

  sema_down (&process_args->child_setup_sema);

  if (process_args->child_start_success)
    list_push_back (&thread_current ()->list_of_children,
                    &process_args->state->elem);
  else
    tid = TID_ERROR;
  // child process fails to load executable or if it fails to allocate memory
  // for child state
  // child process should free all resources it creates

  free_start_process_args (process_args);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct start_process_args *start_process_args = aux;
  struct intr_frame if_;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  lock_acquire (&filesys_lock);
  bool success = load (start_process_args, &if_.eip, &if_.esp);
  lock_release (&filesys_lock);

  if (!success)
    {
      start_process_args->child_start_success = false;
      sema_up (&start_process_args->child_setup_sema);
      // sema_up to let parent run
      // no shared memory between parent and child, thus exit directly
      // do not need to tell parent exit status, since failed to start
      thread_exit ();
      NOT_REACHED ();
    }

  start_process_args->state = init_child_state ();
  if (!start_process_args->state)
    {
      start_process_args->child_start_success = false;
      sema_up (&start_process_args->child_setup_sema);
      // sema_up to let parent run
      // no shared memory between parent and child, thus exit directly
      // do not need to tell parent exit status, since failed to start
      thread_exit ();
      NOT_REACHED ();
    }

  // we don't need lock here because parent process can't run until sema_up
  start_process_args->child_start_success = true;
  thread_current ()->state = start_process_args->state;
  sema_up (&start_process_args->child_setup_sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',

   we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.
 * If it was terminated by the kernel (i.e. killed due to an exception),
 * returns -1.
 * If TID is invalid or if it was not a child of the calling process, or if
 * process_wait() has already been successfully called for the given TID,
 * returns -1 immediately, without waiting.
 *
 * This function will be implemented in task 2.
 * For now, it does nothing. */
int
process_wait (pid_t child_pid)
{
  struct process_child_state *child_state
      = pids_find_and_remove (&thread_current ()->list_of_children, child_pid);

  /* return -1 immediately when pid does not refer to direct child of
     calling process, or process that calls wait has already called wait in pid
   */
  if (child_state == NULL)
    {
      return -1;
    }

  /* Wait until child process_return */
  sema_down (&child_state->wait_sema);

  // child process is already exitted so that no need to lock acquire
  int exit_status = child_state->exit_status;

  return exit_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
           cur->pagedir to NULL before switching page directories,
           so that a timer interrupt can't switch back to the
           process page directory.  We must activate the base page
           directory before destroying the process's page
           directory, or our active page directory will be one
           that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  struct process_child_state *state = cur->state;
  if (state == NOT_INITIALIZE)
    return;

  lock_acquire (&state->lock);
  state->child_exited = true;
  int parent_exited = state->parent_exited;
  lock_release (&state->lock);

  // there won't be concurrency issue between when parent and child process
  // call exit simutaneously because state is only freed when both child and
  // parent process exit is setted to true, this can only happen once

  // whatever the sequence parent and child is exiting the parent both of the
  // child exit and parent exit is setted to true at the end resource get freed

  // the cache value of parent_exit and child_exit might be invalid
  // but this is OK, since the thread (child or parent) get the lock later
  // will always have valid cache, and by then resource get freed
  if (parent_exited)
    free (state);

  lock_acquire (&filesys_lock);
  if (cur->exec_file != NULL)
    file_allow_write (cur->exec_file);
  file_close (cur->exec_file);
  lock_release (&filesys_lock);

  free_file_descriptors (cur);
  free_list_of_children (cur);

  sema_up (&state->wait_sema);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp,
                         struct start_process_args *start_process_args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
static bool
load (void *p_args, void (**eip) (void), void **esp)
{
  struct start_process_args *process_args = p_args;
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (t->name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", t->name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);
      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                             Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                             Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, process_args))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  file_deny_write (file);
  t->exec_file = file;
  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  if (!success)
    file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Check if virtual page already allocated */
      struct thread *t = thread_current ();
      uint8_t *kpage = pagedir_get_page (t->pagedir, upage);

      if (kpage == NULL)
        {

          /* Get a new page of memory. */
          kpage = palloc_get_page (PAL_USER);
          if (kpage == NULL)
            {
              return false;
            }

          /* Add the page to the process's address space. */
          if (!install_page (upage, kpage, writable))
            {
              palloc_free_page (kpage);
              return false;
            }
        }

      /* Load data into the page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, struct start_process_args *process_args)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        {
          *esp = PHYS_BASE;
          size_t available_space = PGSIZE - 512;
          // reserve some spaces for other functions push stack
          // otherwise other functions may easily stack overflow
          
          // we have to have this limit, otherwise it cannot work with the 
          // code already provided

          /* A counter that stores the available space
           * In the check_valid_push_stack and push_stack macro,
           * we check whether the stack has enough space so that
           * we can push things onto the stack.
           * If the space is not enough, set success to false and
           * return directly
           */
          // push the null separated argv
          check_valid_push_stack (process_args->temp_for_build_stack,
                                  process_args->len_argv);

          static const int nullptr = 0;
          char *argv_ptr = PHYS_BASE - 2;
          // zero is at the end of the stack, skip it, so PHYS_BASE - 2


          // a little hack, make sure there are '\0's before the string
          // despite this might not be needed since we initialize the page to all zero
          // but this is still better to add the '\0's, because we might change the
          // palloc flags later
          *(((int *)*esp) - 1) = 0;
          *esp = (void *)word_align (*esp);

          // Push a null pointer 0
          push_stack (nullptr);

          // we have to use this temp due to (argv_ptr + 1) is a right value
          // which doesn't have an address, memcpy cannot work
          char *temp; 
          // Push pointers to the arguments in reverse order
          for (int i = 0; i != process_args->argc;)
            {
              if (*argv_ptr == '\0')
                {
                  temp = argv_ptr + 1;
                  push_stack (temp);
                  i++;
                }
              argv_ptr--;
            }

          // Push a pointer to the first pointer
          // not using push stack, since ambiguity in type
          const char *previous_esp = (*esp);
          check_valid_push_stack (&previous_esp, sizeof (char **));

          // push the number of arguments
          push_stack (process_args->argc);
          push_stack (nullptr); // push a fake return address
        }
      else
        palloc_free_page (kpage);
    }
done:
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Find and remove process_child_state from l and return it.
   if not found, return NULL. */
static struct process_child_state *
pids_find_and_remove (struct list *l, pid_t pid)
{
  if (list_empty (l))
    {
      return NULL;
    }

  for (struct list_elem *cur = list_begin (l); cur != list_end (l);
       cur = list_next (cur))
    {
      struct process_child_state *child_state
          = list_entry (cur, struct process_child_state, elem);
      if (child_state->pid == pid)
        {
          list_remove (cur);
          return child_state;
        }
    }
  return NULL;
}

/* Remove, close file and free file_descriptor for all file_descriptor
   in t's file_descriptors */
static void
free_file_descriptors (struct thread *t)
{
  while (!list_empty (&t->file_descriptors))
    {
      struct list_elem *e = list_pop_front (&t->file_descriptors);
      struct file_descriptor *descriptor
          = list_entry (e, struct file_descriptor, elem);
      lock_acquire (&filesys_lock);
      file_close (descriptor->file);
      lock_release (&filesys_lock);

      free (descriptor);
    }
}

/* Remove, update state and if necessary, free process_child_state
   for all process_child_state in t's list_of_children */
static void
free_list_of_children (struct thread *t)
{
  while (!list_empty (&t->list_of_children))
    {
      struct list_elem *e = list_pop_front (&t->list_of_children);
      struct process_child_state *state
          = list_entry (e, struct process_child_state, elem);

      lock_acquire (&state->lock);
      state->parent_exited = true;
      int child_exited = state->child_exited;
      lock_release (&state->lock);

      // reasoning see process_exit
      if (child_exited)
        free (state);
    }
}

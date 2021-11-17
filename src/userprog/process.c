#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include <list.h>

#define push_stack_size(source, size) \
        do {*esp -= (size);\
            memcpy(*esp, (source), (size));} while (0)
#define push_stack(source) push_stack_size(&(source), sizeof (source))
#define word_align(value) ((unsigned int)(value) & 0xfffffffc)

#define MAX_FILENAME_LEN 14

static thread_func start_process NO_RETURN;
static bool load (void * process_args, void (**eip) (void), void **esp);
static struct process_child_state *pids_find_and_remove (struct list *l, pid_t pid);
static void
free_file_descriptors (struct thread *t);

static void
free_list_of_children (struct thread *t);

/* Passed as argument into start_process. 
   This struct is represented in a page, where load_status pointer is
   copied into first 4 byte, and file_name occupy the remaining of the page*/
struct start_process_args
{
  struct process_load_status load_status;
  struct process_child_state * state;
  char thread_name[MAX_FILENAME_LEN + 1];
  char fn_copy[MAX_ARGV + 1];
  char * ptrs_to_argvs[MAX_ARGC];  
};

static struct process_child_state * init_child_state(void)
{
  struct process_child_state *child_state = malloc(sizeof(struct process_child_state));
  if (!child_state)
    return NULL;

  child_state->pid = thread_current () -> tid;
  child_state->parent_exited = false;
  child_state->child_exited = false;
  child_state->exit_status = 0;
  sema_init(&child_state->wait_sema, 0);
  lock_init(&child_state->lock);

  return child_state;
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute(const char *file_name)
{
  struct process_child_state *child_state = init_child_state();
  if (!child_state)
    return TID_ERROR;

  struct start_process_args *process_args = palloc_get_page (0);
  tid_t tid;

  if (process_args == NULL)
    return TID_ERROR;

  sema_init (&process_args->load_status.done, 0);
  strlcpy (process_args->thread_name, file_name, MAX_FILENAME_LEN + 1);
  char * save_ptr;
  strtok_r (process_args->thread_name, " ", &save_ptr);

  process_args->state = child_state;
  strlcpy (process_args->fn_copy, file_name, MAX_ARGV);
  
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (process_args->thread_name, PRI_DEFAULT, start_process, process_args);

  // PROBLEM: fn_copy and args may not be valid after this func return
  if (tid == TID_ERROR) {
    free (child_state);
    palloc_free_page (process_args); 
  }
  sema_down (&process_args->load_status.done);

  if (process_args->load_status.success) {
    list_push_back (&thread_current ()->list_of_children, &child_state->elem);
  } else {
    // Why not just return TID_ERROR
    tid = -1;
  }

  palloc_free_page(process_args);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct start_process_args *args = (struct start_process_args *) aux;
  struct intr_frame if_;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  // TODO: need lock
  lock_acquire(&filesys_lock);
  args->load_status.success = load (args, &if_.eip, &if_.esp);
  lock_release(&filesys_lock);

  /* if load_status is provided, assign success result into load_status 
     result, and then sema_up relevant semaphore */
  thread_current ()->state = args->state;
  args->state->child_exited = false;
  args->state->pid = thread_current () ->tid;
  args->state->exit_status = -1;
  sema_up (&args->load_status.done);

  //palloc_free_page (args);
  /* If load failed, quit. */
  if (!args->load_status.success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
  
   we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
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
process_wait (tid_t child_tid) 
{  
  struct process_child_state *child_state =
    pids_find_and_remove (&thread_current()->list_of_children, child_tid);

  /* return -1 immediately when pid does not refer to direct child of 
     calling process, or porcess that calls wait has already called wait in pid */
  if (child_state == NULL){
    return -1;
  }

  /* Wait until child process_return */
  // printf("sema down called\n");
  sema_down (&child_state->wait_sema);
  // printf("sema down returned\n");

  lock_acquire(&child_state->lock);
  int exit_status = child_state->exit_status;
  lock_release(&child_state->lock);

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
  struct process_child_state* state =  cur->state;


  lock_acquire(&state->lock);
  state->child_exited = true;
  if (state->parent_exited) {
    lock_release(&state->lock);
    free (cur->state);
  } else {
    lock_release(&state->lock);
  }


  lock_acquire(&filesys_lock);
  if (cur->exec_file != NULL)
    file_allow_write(cur->exec_file);
  file_close(cur->exec_file);
  lock_release(&filesys_lock);

  free_file_descriptors(cur);
  free_list_of_children(cur);

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
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, struct start_process_args* start_process_args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
static bool
load (void * p_args, void (**eip) (void), void **esp) 
{
  struct start_process_args * process_args = p_args;
  const char *file_name = process_args->fn_copy;
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char fn_copy[strlen(file_name) + 1];

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  char *save_ptr;
  strlcpy(fn_copy, file_name, PGSIZE);
  char *f_name = strtok_r(fn_copy, " ", &save_ptr);
  file = filesys_open (f_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", f_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
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
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
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
  *eip = (void (*) (void)) ehdr.e_entry;

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
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
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
      
      if (kpage == NULL){
        
        /* Get a new page of memory. */
        kpage = palloc_get_page (PAL_USER);
        if (kpage == NULL){
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
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
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
setup_stack (void **esp, struct start_process_args * process_args) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        *esp = PHYS_BASE;

        char token_copy_storage[MAX_ARGV + 1];
        memset(token_copy_storage, 0, MAX_ARGV + 1);
        // pointer to current position being copied
        char * token_copy_pos = token_copy_storage;

        /* Count the number of arguments */
        int argc = 0; // Number of arguments
        char *token, *save_ptr;
        int len;
        token = strtok_r (process_args->fn_copy, " ", &save_ptr);
        for (; token != NULL;
            token = strtok_r (NULL, " ", &save_ptr))
        {
          len = strlen(token);
          strlcpy(token_copy_pos, token, len + 1);
          process_args->ptrs_to_argvs[argc++] = token_copy_pos;
          token_copy_pos += len + 2;
        }

        char * arg_addr[argc];
        
        // Push the arguments onto the stack in reverse order
        for (int i = argc - 1; i >= 0; i--)
        {
          size_t token_length = strlen(process_args->ptrs_to_argvs[i]);
          push_stack_size(process_args->ptrs_to_argvs[i], token_length + 1);
          arg_addr[i] = *esp;
        }

        static const int nullptr = 0;

        *esp = (void*) word_align(*esp);

        // Push a null pointer 0
        push_stack(nullptr);

        // Push pointers to the arguments in reverse order
        for (int i = argc - 1; i >= 0; i--)
          push_stack(arg_addr[i]);
                
        // Push a pointer to the first pointer
        // not using push stack, since ambiguity in type
        const char * previous_esp = (*esp);
        push_stack_size(&previous_esp, sizeof (char **));

        push_stack(argc);
        push_stack(nullptr); // push a fake return address
      } 
      else
        palloc_free_page (kpage);
    }
  // hex_dump((uintptr_t)*esp, *esp, PHYS_BASE - *esp, 1);
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

// find the pid of exiting child thread and return its state struct
// if not found return NULL
// remove from list
static struct process_child_state *
pids_find_and_remove (struct list *l, pid_t pid) {
  if (list_empty (l)){
    return NULL;
  }
  
  for (struct list_elem *cur = list_begin (l); cur != list_end (l); cur = list_next (cur)) {
    struct process_child_state * child_state = list_entry (cur, struct process_child_state, elem);
    if (child_state->pid == pid) {
      list_remove (cur);
      return child_state;
    }
  }
  return NULL;
}

static void
free_file_descriptors (struct thread *t)
{
  while (!list_empty (&t->file_descriptors)) {
    struct list_elem *e = list_pop_front (&t->file_descriptors);
    struct file_descriptor *desciptor = list_entry (e, struct file_descriptor, elem);
    lock_acquire(&filesys_lock);
    file_close (desciptor->file);
    lock_release(&filesys_lock);

    free (desciptor);
  }
}

static void
free_list_of_children (struct thread *t)
{
  while (!list_empty (&t->list_of_children)) {
    struct list_elem *e = list_pop_front (&t->list_of_children);
    struct process_child_state *state = list_entry (e, struct process_child_state, elem);

    // reading and writing state, thus locked
    lock_acquire(&state->lock);
    state->parent_exited = true;
    if (state->child_exited) {
      free (state);
    };
    lock_release(&state->lock);
  }
}

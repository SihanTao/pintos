#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static int syscall_id_argc_map[] =
{
  0, // SYS_HALT,                   /* Halt the operating system. */
  1, // SYS_EXIT,                   /* Terminate this process. */
  1, // SYS_EXEC,                   /* Start another process. */
  1, // SYS_WAIT,                   /* Wait for a child process to die. */
  2, // SYS_CREATE,                 /* Create a file. */
  1, // SYS_REMOVE,                 /* Delete a file. */
  1, // SYS_OPEN,                   /* Open a file. */
  1, //  SYS_FILESIZE,               /* Obtain a file's size. */
  3, //   SYS_READ,                   /* Read from a file. */
  3, //  SYS_WRITE,                  /* Write to a file. */
  2, //  SYS_SEEK,                   /* Change position in a file. */
  1, //  SYS_TELL,                   /* Report current position in a file. */
  1, //  SYS_CLOSE,                  /* Close a file. */

    // /* Task 3 and optionally task 4. */
    // SYS_MMAP,                   /* Map a file into memory. */
    // SYS_MUNMAP,                 /* Remove a memory mapping. */

    // /* Task 4 only. */
    // SYS_CHDIR,                  /* Change the current directory. */
    // SYS_MKDIR,                  /* Create a directory. */
    // SYS_READDIR,                /* Reads a directory entry. */
    // SYS_ISDIR,                  /* Tests if a fd represents a directory. */
    // SYS_INUMBER,                /* Returns the inode number for a fd. */
    
    // END_SYS_CALL,
};

static void sys_halt_handler (struct intr_frame *, int, int, int);
static void sys_exit_handler (struct intr_frame *, int, int, int);
static void sys_wait_handler (struct intr_frame *, int, int, int);
static void sys_create_handler (struct intr_frame *, int, int, int);
static void sys_remove_handler (struct intr_frame *, int, int, int);
static void sys_open_handler (struct intr_frame *, int, int, int);
static void sys_filesize_handler (struct intr_frame *, int, int, int);
static void sys_read_handler (struct intr_frame *, int, int, int);
static void sys_write_handler (struct intr_frame *, int, int, int);
static void sys_seek_handler (struct intr_frame *, int, int, int);
static void sys_tell_handler (struct intr_frame *, int, int, int);
static void sys_close_handler (struct intr_frame *, int, int, int);

static void resolve_syscall_stack (int argc, void * stack_pointer, int * output)
{
  for (int i = 0; i < argc; i++){
    output[i] = ((int *) stack_pointer)[i];
  }
}

static void syscall_handler (struct intr_frame *);
typedef void (*syscall_func_t) (struct intr_frame *, int, int, int);
syscall_func_t syscall_funcs[] =
  {
    sys_halt_handler,
    sys_exit_handler,
    sys_wait_handler,
    sys_create_handler,
    sys_remove_handler,
    sys_open_handler,
    sys_filesize_handler,
    sys_read_handler,
    sys_write_handler,
    sys_seek_handler,
    sys_tell_handler,
    sys_close_handler
  };


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * stack_ptr = f->esp;
  int sys_argv[] = {0, 0, 0}; // must initialize to 0 !!!
                              // don't change 

  check_safe_memory_access(stack_ptr);
  check_safe_memory_access(stack_ptr + 1);

  int syscall_number = *(int*) stack_ptr;
  resolve_syscall_stack (syscall_id_argc_map[syscall_number], stack_ptr, sys_argv);

  printf("*esp = %d\n", syscall_number);

  syscall_funcs[syscall_number](f, sys_argv[0], sys_argv[1], sys_argv[2]);
}

/*
 * Check whether the VADDR is safe.
 * Otherwise exit the thread.
 */
void* check_safe_memory_access(const void* vaddr)
{
  struct thread * cur = thread_current();

  if (!is_user_vaddr(vaddr)) {
    thread_exit();
  }

  void * kaddr = pagedir_get_page(cur->pagedir, vaddr);
  // not sure if this is correct

  if (kaddr == NULL)
  {
    thread_exit();
  }

  return kaddr;
  
}

void sys_exit_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  printf("%s: exit(%d)\n", thread_current()->name, 0);
  thread_exit ();
  // not sure what to do
}

void sys_write_handler (struct intr_frame * f UNUSED, int fd, int buffer, int size UNUSED)
{
  if (fd == 1)
  {
  }
  // not sure what to do
}

static void sys_halt_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_wait_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_create_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_remove_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_open_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_filesize_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_read_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_seek_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_tell_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
static void sys_close_handler (struct intr_frame * f UNUSED, int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {}
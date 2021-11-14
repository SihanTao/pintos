#include <stddef.h>
#include <list.h>
#include "lib/stdio.h"
#include "userprog/syscall.h"
#include "lib/kernel/stdio.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static struct lock filesys_lock;
static struct lock fd_hash_lock;

static int sys_halt_handler (int, int, int);
static int sys_exit_handler ( int, int, int);
static int sys_exec_handler ( int, int, int);
static int sys_wait_handler ( int, int, int);
static int sys_create_handler ( int, int, int);
static int sys_remove_handler ( int, int, int);
static int sys_open_handler ( int, int, int);
static int sys_filesize_handler ( int, int, int);
static int sys_read_handler ( int, int, int);
static int sys_write_handler ( int, int, int);
static int sys_seek_handler ( int, int, int);
static int sys_tell_handler ( int, int, int);
static int sys_close_handler ( int, int, int);
static struct file * to_file(int fd);

static void syscall_handler (struct intr_frame *);

typedef int (*syscall_func_t) ( int, int, int);
static syscall_func_t syscall_funcs[] =
  {
    [SYS_HALT] = sys_halt_handler,
    [SYS_EXIT] = sys_exit_handler,
    [SYS_EXEC] = sys_exec_handler,
    [SYS_WAIT] = sys_wait_handler,
    [SYS_CREATE] = sys_create_handler,
    [SYS_REMOVE] = sys_remove_handler,
    [SYS_OPEN] = sys_open_handler,
    [SYS_FILESIZE] = sys_filesize_handler,
    [SYS_READ] = sys_read_handler,
    [SYS_WRITE]= sys_write_handler,
    [SYS_SEEK] = sys_seek_handler,
    [SYS_TELL] = sys_tell_handler,
    [SYS_CLOSE] = sys_close_handler
  };

static int argc_syscall[] =
  {
    [SYS_HALT] = 0,
    [SYS_EXIT] = 1,
    [SYS_EXEC] = 1,
    [SYS_WAIT] = 1,
    [SYS_CREATE] = 2,
    [SYS_REMOVE] = 1,
    [SYS_OPEN] = 1,
    [SYS_FILESIZE] = 1,
    [SYS_READ] = 3,
    [SYS_WRITE]= 3,
    [SYS_SEEK] = 2,
    [SYS_TELL] = 1,
    [SYS_CLOSE] = 1
  };

static void resolve_syscall_stack (int argc, void * stack_pointer, int * output)
{
  for (int i = 0; i < argc; i++){
    output[i] = ((int *) stack_pointer)[i + 1];
  }
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  lock_init(&fd_hash_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * stack_ptr = f->esp;
  int sys_argv[] = {0, 0, 0}; // must initialize to 0 !!!
                              // don't change 

  for (int i = 0; i < argc_syscall[i] + 1; i++){
    check_safe_memory_access(stack_ptr + i);
  }

  int syscall_number = *(int*) stack_ptr;
  // struct thread * cur = thread_current();
  resolve_syscall_stack (argc_syscall[syscall_number], stack_ptr, sys_argv);

  // printf("*esp = %d, from %s thread, id == %d\n", syscall_number, cur->name, cur->tid);

  f->eax = syscall_funcs[syscall_number](sys_argv[0], sys_argv[1], sys_argv[2]);
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

  if (kaddr == NULL)
  {
    thread_exit();
  }

  return kaddr;
  
}

int sys_exit_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  // printf("%s: exit(%d)\n", thread_current()->name, 0);
  thread_exit ();
  // not sure what to do
  return 0;
}

static int sys_write_handler ( int fd, int buffer, int size)
{
  if (fd == STDOUT_FILENO)
  {
    putbuf((char *) buffer, (size_t) size);
    return size;
  }

  if (fd == STDIN_FILENO)
    sys_exit_handler(-1, -1, -1);

  struct file * file = to_file(fd);
  if (file == NULL)
    sys_exit_handler(-1, -1, -1);
  
  return file_write(file, (const void *) buffer, (off_t) size);
}

static int sys_halt_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {
  sys_exit_handler(0, 0, 0);
  return 0;
}
static int sys_exec_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) 
  {
    return 0;
  }
static int sys_wait_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }

static int sys_create_handler ( int file_name, int size, int arg2 UNUSED)
{ 
  check_safe_memory_access((const void *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_create((const char *) file_name, (off_t) size);
  lock_release(&filesys_lock);
  
  return (int) output; 
}

static int sys_remove_handler ( int file_name, int arg1 UNUSED, int arg2 UNUSED)
{
  check_safe_memory_access((const void *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_remove((const char *) file_name);
  lock_release(&filesys_lock);

  return (int) output; 
}

int sys_open_handler (int file_name, int arg1 UNUSED, int arg2 UNUSED)
{ 
  check_safe_memory_access((const void *) file_name); 
  struct file * file;
  struct file_descriptor file_descriptor;
  struct thread * cur = thread_current();

  lock_acquire(&filesys_lock);

  if (!(file = filesys_open((const char *) file_name)))
  {
    lock_release(&filesys_lock);
    return -1;
  }

  file_descriptor.file = file;
  file_descriptor.fd = cur -> fd_incrementor++;
  list_push_back(&cur->file_descriptors, &file_descriptor.elem);

  lock_release(&filesys_lock);
  return file_descriptor.fd;
}

static int sys_filesize_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  lock_acquire (&filesys_lock);
  struct file * file = to_file(fd);

  if(fd == NULL) {
    lock_release (&filesys_lock);
    return -1;
  }

  int ret = file_length(file);
  lock_release (&filesys_lock);
  return ret;
}
static int sys_read_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_seek_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_tell_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_close_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }

// pre : wrapped by file_system locks!
// find file according to fd in current thread
// if fd not exist, return NULL
static struct file * to_file(int fd)
{
  struct thread * cur = thread_current();
  struct list * fds = &cur->file_descriptors;
  struct list_elem * e;
  struct file_descriptor * file_descriptor;

  for (e = list_begin(fds); e != list_end(fds) ; e = list_next(e))
  {
    file_descriptor = list_entry(e, struct file_descriptor, elem);
    if (file_descriptor->fd == fd)
    {
      return file_descriptor->file;
    }
  }

  return NULL;
}
#include <stddef.h>
#include <list.h>
#include "lib/stdio.h"
#include "userprog/syscall.h"
#include "lib/kernel/stdio.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "devices/input.h"
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
static struct file_descriptor * to_file_descriptor(int fd);
static void* check_safe_memory_access(void* vaddr);


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

  int syscall_number = *(int32_t*) stack_ptr;
  // struct thread * cur = thread_current();
  resolve_syscall_stack (argc_syscall[syscall_number], stack_ptr, sys_argv);

  // printf("*esp = %d, from %s thread, id == %d\n", syscall_number, cur->name, cur->tid);

  f->eax = syscall_funcs[syscall_number](sys_argv[0], sys_argv[1], sys_argv[2]);
}

/*
 * Check whether the VADDR is safe.
 * Otherwise exit the thread.
 */
void* check_safe_memory_access(void* vaddr)
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

/* Terminates Pintos */
static int sys_halt_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  shutdown_power_off ();
  return 0;
}

/* Set the current thread's exit status to 0 and the exit status to true */
int sys_exit_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  thread_current ()->process_ref->exit_status = arg0;
  thread_current ()->process_ref->exited = true;
  process_exit ();
  return 0;
}

/* Runs the given cmd_line and return the the PID of the new process */
static int sys_exec_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) 
{
  const char *cmd_line = (char *) arg0;
  int pid;
  struct process_load_status status;
  struct process_state *child_state = calloc(1, sizeof(struct process_state));
  sema_init (&status.done, 0);
  
  pid = process_execute_inner (cmd_line, &status, child_state);

  if (pid == TID_ERROR)
    return -1;
    // wait until the other thread finished loading the executable; sema_up
    // will be called by the thread created by process_execute_inner 
  sema_down (&status.done);

  if (!status.success)
    return -1;

  struct thread *t = thread_current ();
  child_state->pid = pid;
  child_state->exited = false;
  list_push_back (&t->list_of_child_process, &child_state->elem);
  // add the child state to the list of child process of the current thread 

  return pid;
}

/* Waits for a child process pid and retrieves the child’s exit status */
static int sys_wait_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {
  tid_t pid = (tid_t) arg0;
  return process_wait (pid);
}


/* Check if the file has a valid virtual address and call filesys_create to create the file.*/
static int sys_create_handler ( int file_name, int size, int arg2 UNUSED)
{ 
  check_safe_memory_access((void *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_create((const char *) file_name, (off_t) size);
  lock_release(&filesys_lock);
  
  return (int) output; 
}

/* Check if the file has a valid virtual address and call filesys_remove to remove */
static int sys_remove_handler ( int file_name, int arg1 UNUSED, int arg2 UNUSED)
{
  check_safe_memory_access((char *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_remove((const char *) file_name);
  lock_release(&filesys_lock);

  return (int) output; 
}

/* Open the file and add its file descriptor to the list of opened files 
  of the current thread */
int sys_open_handler (int file_name, int arg1 UNUSED, int arg2 UNUSED)
{ 
  check_safe_memory_access((char *) file_name); 
  struct file * file;
  struct file_descriptor file_descriptor;
  struct thread * cur = thread_current();

  lock_acquire(&filesys_lock);

  if (!(file = filesys_open((const char *) file_name)))
  {
    lock_release(&filesys_lock);
    return -1;
  } 
  //Call filesys_open to open the file, if fails then return -1

  file_descriptor.file = file;
  file_descriptor.fd = cur -> fd_incrementor++;
  // Set the file and fd in the file_descriptor to initialized fild
  list_push_back(&cur->file_descriptors, &file_descriptor.elem);
  // Add the file_descriptor to the end of the opened filed of the current thread

  lock_release(&filesys_lock);
  return file_descriptor.fd;
}

/* Call file_length to return the file size */
static int sys_filesize_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  lock_acquire (&filesys_lock);
  struct file * file = to_file(fd);

  if(file== NULL) {
    lock_release (&filesys_lock);
    return -1;
  }

  int ret = file_length(file);
  lock_release (&filesys_lock);
  return ret;
}

/* Read the file open as fd into buffe given the size bytes */
static int sys_read_handler ( int fd, int buffer, int size)
{
  for (int i = 0; i < size; i++)
    check_safe_memory_access((char *) buffer + i);

  lock_acquire (&filesys_lock);

  if(fd == STDIN_FILENO) 
  {
    for(int i = 0; i < size; i++)
      ((char *) buffer)[i] = input_getc();
    lock_release (&filesys_lock);
    return size;
  }
  // read the file into buffer. 

  struct file * file = to_file(fd);
  int ret = file ? file_read(file, (char *) buffer, size) : -1;
  //check if file exist.

  lock_release (&filesys_lock);
  return ret;
}

/* Get the file with the given fd and call file_seek to set the next
  byte to a given position */
static int sys_seek_handler (int fd, int position, int arg2 UNUSED) 
{ 
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
      return 0;

  lock_acquire (&filesys_lock);
  struct file *file = to_file (fd);
  if (!file)
    return 0;
  file_seek (file, position);
  lock_release (&filesys_lock);
  return 0; 
}

/* Return the position of the next byte to be read or written */
static int sys_tell_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
      return 0;

  lock_acquire (&filesys_lock);
  struct file *file = to_file (fd);
  if (!file)
    return 0;
  file_tell (file);
  lock_release (&filesys_lock);
  return 0;
}

/* Close the file and remove it from the list of threads that opens the file */
static int sys_close_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  lock_acquire (&filesys_lock);
  struct file_descriptor * file_descriptor = to_file_descriptor(fd);
  if (!file_descriptor)
    return 0;
    //check if the file descriptor exist. 

  file_close (file_descriptor->file);
  list_remove (&file_descriptor->elem);
  lock_release (&filesys_lock);

  return 0;
}

// pre : wrapped by file_system locks!
// find file according to fd in current thread
// if fd not exist, return NULL
 struct file * to_file(int fd)
{
  struct file_descriptor * file_descriptor = to_file_descriptor(fd);
  if (!file_descriptor)
    return NULL;

  return file_descriptor->file;
}

inline struct file_descriptor * to_file_descriptor(int fd)
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
      return file_descriptor;
    }
  }

  return NULL;
}


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
static void check_safe_memory_access(const void* vaddr);
static void check_ranged_memory (const void * start, size_t length, size_t size_of_type);
static void check_string_memory (const char * start);


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

/* Add all the arguments from stack to output buffer */
static void check_and_resolve_syscall_stack (int argc, int * stack_pointer, int * output)
{
  for (int i = 0; i < argc; i++){
    check_safe_memory_access(stack_pointer + i + 1);
    output[i] = stack_pointer[i + 1];
  }
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * stack_ptr = f->esp;
  check_safe_memory_access(stack_ptr);
  int sys_argv[] = {0, 0, 0}; // must initialize to 0 !!!
  int syscall_number = *(int32_t*) stack_ptr;
  check_and_resolve_syscall_stack (
    argc_syscall[syscall_number], stack_ptr, sys_argv);
  f->eax = syscall_funcs[syscall_number](sys_argv[0], sys_argv[1], sys_argv[2]);
}

/*
 * Check whether the VADDR is safe.
 * Otherwise exit the thread.
 */
void check_safe_memory_access(const void* vaddr)
{
  struct thread * cur = thread_current();

  if (!is_user_vaddr(vaddr) || vaddr == NULL) 
    exit_wrapper(-1);

  void * kaddr = pagedir_get_page(cur->pagedir, vaddr);

  if (kaddr == NULL)
    exit_wrapper(-1);  
}

// write to file if file is opened and held by this thread
// else exit(-1)
// if fd == STDOUT_FILENO write to stdout
// if fd == STDIN_FILENO exit(-1)
static int sys_write_handler ( int fd, int buffer, int size)
{
  check_ranged_memory((char *) buffer, size, sizeof(char));

  if (fd == STDOUT_FILENO)
  {
    putbuf((char *) buffer, (size_t) size);
    return size;
  }

  if (fd == STDIN_FILENO)
    exit_wrapper(-1);

  struct file * file = to_file(fd);
  if (file == NULL)
    exit_wrapper(-1);

  lock_acquire(&filesys_lock);
  int ret = file_write(file, (const void *) buffer, (off_t) size);
  lock_release(&filesys_lock);

  return ret;
}

static int sys_halt_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  shutdown_power_off ();
  NOT_REACHED ();
}

int sys_exit_handler ( int exit_status, int arg1 UNUSED, int arg2 UNUSED)
{
  struct process_child_state *state = thread_current ()->state;
  lock_acquire(&state->lock);
  state->exit_status = exit_status;
  lock_release(&state->lock);

  printf("%s: exit(%d)\n", thread_current ()->name, exit_status);
  thread_exit ();
  NOT_REACHED ();
}

static int sys_exec_handler ( int cmd_line, int arg1 UNUSED, int arg2 UNUSED) 
{
  check_string_memory((const char *) cmd_line);

  return process_execute ((char *) cmd_line);
}

static int sys_wait_handler ( int pid, int arg1 UNUSED, int arg2 UNUSED) {
  return process_wait ((tid_t) pid);
}


/* Check if the file has a valid virtual address and call filesys_create to create the file.*/
static int sys_create_handler ( int file_name, int size, int arg2 UNUSED)
{ 
  check_string_memory((const char *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_create((const char *) file_name, (off_t) size);
  lock_release(&filesys_lock);
  
  return (int) output; 
}

/* Check if the file has a valid virtual address and call filesys_remove to remove */
static int sys_remove_handler ( int file_name, int arg1 UNUSED, int arg2 UNUSED)
{
  check_string_memory((const char *) file_name);

  lock_acquire(&filesys_lock);
  bool output = filesys_remove((const char *) file_name);
  lock_release(&filesys_lock);

  return (int) output; 
}

/* Open the file and add its file descriptor to the list of opened files 
  of the current thread */
int sys_open_handler (int file_name, int arg1 UNUSED, int arg2 UNUSED)
{ 
  check_string_memory((const char *) file_name);
  
  struct file_descriptor * file_descriptor = 
                    malloc(sizeof (struct file_descriptor)); 
  if (!file_descriptor)
    exit_wrapper(-1);

  lock_acquire(&filesys_lock);
  struct file * file = filesys_open((const char *) file_name);
  lock_release(&filesys_lock);

  if (!file) {
    free (file_descriptor);
    return -1;
  }

  struct thread * cur = thread_current();
  file_descriptor->file = file;
  file_descriptor->fd = cur -> fd_incrementor++;
  list_push_back(&cur->file_descriptors, &file_descriptor->elem);
  // only one thread can access to its file descriptor list, thus
  // doesn't need to be protected by locks

  return file_descriptor->fd;
}

/* Call file_length to return the file size */
static int sys_filesize_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  // only one thread can access to its file descriptor list, thus
  // doesn't need to be protected by locks
  struct file * file = to_file(fd);
  if(file== NULL)
    return -1;

  lock_acquire (&filesys_lock);
  int ret = file_length(file);
  lock_release (&filesys_lock);
  return ret;
}

/* Read the file open as fd into buffe given the size bytes 
  if read from stdin, read put stdin to buffer
  else find file and read from file
  if file doesn't exist, return -1
*/
static int sys_read_handler ( int fd, int buffer, int size)
{
  check_ranged_memory((char *) buffer, size, sizeof(char));

  if(fd == STDIN_FILENO) 
  {
    for(int i = 0; i < size; i++)
      ((char *) buffer)[i] = input_getc();
    return size;
  }
  // read the stdin into buffer. 

  struct file * file = to_file(fd);
  if (!file)
    return -1;

  lock_acquire (&filesys_lock);
  int ret = file_read(file, (char *) buffer, size);
  lock_release (&filesys_lock);
  return ret;
}

/* Get the file with the given fd and call file_seek to set the next
  byte to a given position */
static int sys_seek_handler (int fd, int position, int arg2 UNUSED) 
{ 
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
      return 0;

  struct file *file = to_file (fd);
  if (!file)
    return 0; // TODO check the return code!!!

  lock_acquire (&filesys_lock);
  file_seek (file, position);
  lock_release (&filesys_lock);
  return 0; 
}

/* Return the position of the next byte to be read or written */
static int sys_tell_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
      return 0;

  struct file *file = to_file (fd);
  if (!file)
    return 0; // TODO check the return code!!!

  lock_acquire (&filesys_lock);
  file_tell (file);
  lock_release (&filesys_lock);
  return 0;
}

/* Close the file and remove it from the list of threads that opens the file */
static int sys_close_handler ( int fd, int arg1 UNUSED, int arg2 UNUSED)
{ 
  struct file_descriptor * file_descriptor = to_file_descriptor(fd);
  if (!file_descriptor) 
    return 0;

  lock_acquire (&filesys_lock);
  file_close (file_descriptor->file);
  lock_release (&filesys_lock);

  list_remove (&file_descriptor->elem);
  free(file_descriptor);

  return 0;
}

// find file according to fd in current thread
// if fd not exist, return NULL
 struct file * to_file(int fd)
{
  struct file_descriptor * file_descriptor = to_file_descriptor(fd);
  if (!file_descriptor)
    return NULL;

  return file_descriptor->file;
}

// since only one thread can access to its own file descriptor list
// thus doesn't need to be protected by locks
inline struct file_descriptor * to_file_descriptor(int fd)
{
  struct thread * cur = thread_current();
  struct list * fds = &cur->file_descriptors;
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || list_empty (fds))
    return NULL;
  struct list_elem * e;
  struct file_descriptor * file_descriptor;

  for (e = list_begin(fds); e != list_end(fds) ; e = list_next(e))
  {
    file_descriptor = list_entry(e, struct file_descriptor, elem);
    if (file_descriptor->fd == fd)
      return file_descriptor;
  }

  return NULL;
}

int exit_wrapper(int status)
{
  sys_exit_handler(status, 0, 0);
  NOT_REACHED ();
}

void check_string_memory (const char * start)
{
  check_safe_memory_access(start);

  void * check_next = pg_round_up(start);

  // this is valid in current stage, because user programs cannot call malloc
  // memory is allocated by whole pages in user space, which we only check 
  // the start of each page after the start 
  //
  // this is correct since the start of each page the string going through is
  // valid
  //
  // if the end is not at page boundary we doesn't need to check the end 
  // since the start of the page where the end lies in is checked to be valid
  // thus the whole page is valid

  for (size_t i = 0; ; i++)
  {
    if (check_next == start + i) {
      check_safe_memory_access(check_next);
      check_next += PGSIZE;
    }
    if (start[i] == '\0')
      break;
  }
}

// since only one thread can access to its own file descriptor list
// thus doesn't need to be protected by locks
void check_ranged_memory (const void * start, size_t length, size_t size_of_type) 
{
  check_safe_memory_access(start);

  // void * + offset is undefined behaviour!!! 
  // (despite void * work as char * in gcc)
  // thus void * is casted to char * 
  void * end = (char *) start + (length * size_of_type);
  void * rounded_down_end = pg_round_up(end);
  for ( void * cur = pg_round_up(start);
        cur != rounded_down_end;
        cur += PGSIZE ) 
  {
    check_safe_memory_access(cur);
  }
  // if rouned_up_end == rounded_up_start
  // end and start are in the same page
  // it is sufficient to check this page is valid
  // thus only need to check the end pointer is valid

  // else check every page this chunk of memory going through
  // haven't checked the end page
  check_safe_memory_access(end);
}
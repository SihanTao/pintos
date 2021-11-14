#include <stddef.h>
#include <stdio.h>
#include "userprog/syscall.h"
#include "lib/kernel/stdio.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/malloc.h"

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


static int sys_write_handler ( int fd, int buffer, int size)
{
  // printf("write is called\n");
  // printf("fd == %d\n", fd);
  printf("%p, %u\n", (void*)buffer, size);
  if (fd == 1)
  {
    putbuf((char *) buffer, size);
    // printf("%s", (char *) buffer);
  }
  // not sure what to do
  return 0;
}

static int sys_halt_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  shutdown_power_off ();
  return 0;
}

int sys_exit_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED)
{
  thread_current ()->process_ref->exit_status = arg0;
  thread_current ()->process_ref->exited = true;
  process_exit ();
  return 0;
}

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
  
  sema_down (&status.done);

  if (!status.success)
    return -1;

  struct thread *t = thread_current ();
  child_state->pid = pid;
  child_state->exited = false;
  list_push_back (&t->list_of_child_process, &child_state->elem);
  
  return pid;
}

static int sys_wait_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) {
  tid_t pid = (tid_t) arg0;
  return process_wait (pid);
}

static int sys_create_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_remove_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_open_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_filesize_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_read_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_seek_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_tell_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }
static int sys_close_handler ( int arg0 UNUSED, int arg1 UNUSED, int arg2 UNUSED) { return 0; }



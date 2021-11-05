#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * stack_ptr = f->esp;

  int syscall_number = *(int*) stack_ptr;

  printf("*esp = %d\n", syscall_number);

  // if the command is exit
  check_safe_memory_access(stack_ptr);
  check_safe_memory_access(stack_ptr + 1);
  printf("%s: exit(%d)\n", thread_current()->name, 0);
  thread_exit ();

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


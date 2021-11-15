#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#ifndef USERPROG
#define USERPROG
#endif

#include "threads/thread.h"
#include "threads/malloc.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "threads/synch.h"

typedef tid_t pid_t;

#define MAX_ARGC 128
#define MAX_ARGV 512 // not sure about length
struct process_load_status
{
  struct semaphore done;
  bool success;
};

struct process_state {
  bool exited;
  pid_t pid;
  int exit_status;
  struct thread *child;
  struct list_elem elem;
};

tid_t process_execute (const char *file_name);
tid_t process_execute_inner (const char *file_name,
			     struct process_load_status *load_status UNUSED,
			     struct process_state *state UNUSED);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */

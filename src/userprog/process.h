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

/* Used in struct start_process_args to store the load status for threads */
struct process_load_status
{
  struct semaphore done;        // Used by parent process to wait for child to finish loading
  bool success;                 // To indicate whether child process has load successfully
};

/* A structure to store the state of child process */
struct process_child_state {
  struct lock lock;             // To prevent race condition between parent process and child process
  bool child_exited;            // To indicate if child process exited 
  bool parent_exited;           // To indicate if parent process exited
  pid_t pid;                    // pid of child process 
  int exit_status;              // Exit status of child process
  struct list_elem elem;        // Used by thread to store list of child process
  struct semaphore wait_sema;   // Used by parent process to wait for child process
};

tid_t process_execute (const char *file_name);   
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */

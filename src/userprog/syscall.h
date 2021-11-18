#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifndef USERPROG
#define USERPROG
#endif

#include "pagedir.h"
#include "../threads/vaddr.h"
#include <list.h>

struct lock filesys_lock;

/* Assign a indicator to each file */
struct file_descriptor
{
    int fd;                 /* A unique indicator for each use of file */
    struct file * file;     /* A pointer to the file indicated to */
    struct list_elem elem;  /* Threads that opens the file */
};


void syscall_init (void);
int exit_wrapper(int status);

#endif /* userprog/syscall.h */

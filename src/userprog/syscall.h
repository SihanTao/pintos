#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifndef USERPROG
#define USERPROG
#endif

#include "pagedir.h"
#include "../threads/vaddr.h"
#include <list.h>

struct file_descriptor
{
    int fd;
    struct file * file;
    struct list_elem elem;
};

void* check_safe_memory_access(const void* vaddr);
void syscall_init (void);

#endif /* userprog/syscall.h */

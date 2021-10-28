/* a interface of ready list, can be one or 64 list */

#ifndef _THREAD_READY_LIST_H
#define _THREAD_READY_LIST_H

#include "thread.h"
#include <stdlib.h>
#include "synch.h"

struct lock ready_list_lock;

extern struct thread* poll_ready_list(void);
extern void push_ready_list(struct thread *);
extern size_t size_ready_list (void);
extern void init_ready_list(void);
extern int heighest_priority_in_ready_list(void);
#define ready_list_lock_acquire() do(lock_acquire(&ready_list_lock))while(0)
#define ready_list_lock_release() do(lock_release(&ready_list_lock))while(0)

#endif //_THREAD_READY_LIST_H

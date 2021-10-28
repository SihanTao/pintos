/* a interface of ready list, can be one or 64 list */

#ifndef _THREAD_READY_LIST_H
#define _THREAD_READY_LIST_H

#include "thread.h"
#include <stdlib.h>

extern struct thread* poll_ready_list(void);
extern void push_ready_list(struct thread *);
extern size_t size_ready_list (void);
extern void init_ready_list(void);
extern int heighest_priority_in_ready_list(void);

#endif //_THREAD_READY_LIST_H

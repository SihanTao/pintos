#include "thread.h"
#include "ready_list.h"
#include "threads/interrupt.h"
#include "synch.h"
#include <list.h>
#include <debug.h>

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_queue[64];

// invariant : highest ready priority === max(priority(ready_queue)) 
//                    if      cache n ready thread >= 0
//                    else    0
static int cache_highest_ready_priority; 

// invariant : cache n ready thread === size(ready_queue)
static int cache_n_ready_thread;

/*macro-scopic :
    return the highest prioritized thread in the ready list
    
    pre : protected by ready list lock || intr_off

  micro-scopic :
    return the highest prioritized thread in the ready list

    re-calculate the cache highest ready priority, given that the old
    value is larger or equal to the real value

    reduce the size of ready list by 1
*/
static struct thread * poll_ready_list(void){
  ASSERT (intr_get_level() == INTR_OFF);

  cache_n_ready_thread--;
  struct list_elem *e = list_pop_front (ready_queue + cache_highest_ready_priority);

  if (list_empty (ready_queue + cache_highest_ready_priority))
    {
      for (int i = cache_highest_ready_priority; i > -1; i--)
        if (!list_empty (ready_queue + i))
          {
            cache_highest_ready_priority = i;
            return list_entry (e, struct thread, elem);
          }
      cache_highest_ready_priority = 0;
    }
  return list_entry (e, struct thread, elem);
}

/* macro scopic:
  put one thread into ready list

  pre : protected by ready list lock || intr_off

  micro scopic:
    Put the thread into corresponding ready queue

    Update the highest ready priority variable. (re-establish heighest ready priority invariant)

    Increment the n ready threads
     (re-establish n ready threads invariant)

*/
void push_ready_list(struct thread *t)
{
  ASSERT( intr_get_level () == INTR_OFF);
  int new_t_priority = thread_get_effective_priority (t);
  cache_highest_ready_priority = max (new_t_priority, cache_highest_ready_priority);
  cache_n_ready_thread++;
  list_push_back (ready_queue + new_t_priority, &t->elem);
}

// pre : protected by ready list lock || intr_off
size_t size_ready_list (void)
{
    return cache_n_ready_thread;
}

// pre : protected by ready list lock || intr_off
int heighest_priority_in_ready_list(void)
{
    return cache_highest_ready_priority;
}

void init_ready_list(void)
{
    cache_n_ready_thread = 0;
    cache_highest_ready_priority = 0;
    lock_init(&ready_list_lock);
    for (int i = 0; i < 64; i++)
        list_init(ready_queue + i);
}

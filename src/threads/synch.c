/* This file is derived from source code for the NachosA
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>

#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define bound(x, low, high) (max (min ((x), (high)), (low)))

static void donate_lock_priority (struct lock *l, int new_priority);
static void donate_thread_priority (struct thread *t, int new_priority);
static int recalc_cached_lock_priority (struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* 1. The highest prioritized thread in sema's waiter list is put to ready
   list.

   2. If the recent woke up thread has higher priority than the running thread,
   switch to the new woke up thread.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;

  if (!list_empty (&sema->waiters))
    {
      // the highest prioritized thread in sema's waiter list is put in ready
      // list;
      struct list_elem *e
          = list_max (&sema->waiters, less_thread_effective_priority, NULL);
      list_remove (e);
      // If the recent woke up thread has higher priority than the running
      // thread, switch to the new woke up thread.
      struct thread *t = list_entry (e, struct thread, elem);
      thread_unblock (t);

      struct thread *cur = thread_current ();
      bool flag = thread_mlfqs ? t->priority > cur->priority
                               : (t->cached_priority > cur->cached_priority);

      if (flag)
        intr_context () ? intr_yield_on_return () : thread_yield ();
      // pre of thread yield : !intr_context() but this function will not be
      // called in intr_context given : max_priority(ready_list) <= priority
      // (running thread) known : priority (waking thread) > priority (running
      // thread)
      //                                  >= max_priority(ready_list)
      // thus we will always get the waking thread from ready list if it is the
      // highest prioritized thread
    }

  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->cached_priority = 0;
}

/*  if lock holder is held by other threads then put the thread into lock's
   waiter list; donate priority to the lock and the threads that holding that
   lock layer by layer

   block the thread if other thread is holding the lock

   if the thread holds the lock
   since the thread might be removed from the waiter list
   recalc lock cached priority

  pre : !intr_context()


Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  enum intr_level old_level = intr_disable ();
  // must disable intr because while donating priority
  // another thread should not acquire the lock
  // cannot use a lock to protect a lock (recurr ) //TODO : use a sema
  if (lock->holder != NULL)
    {
      cur->lock_waiting = lock;
      if (cur->cached_priority > lock->cached_priority)
        donate_lock_priority (lock, cur->cached_priority);
    }

  // block the thread if the lock is held by other threads else
  // hold the lock by sema_down

  // inside sema down, (intr_level === INTR_OFF)
  // so that the thread is correctly added to the waiter list
  // lock->cached_priority === list_max_priority(waiters)
  sema_down (&lock->semaphore);
  // after sema down intr_level === INTR_OFF

  // if the thread holds the lock
  // since the thread might be removed from the waiter list
  // recalc lock cached priority
  cur->lock_waiting = NULL;
  lock->cached_priority = recalc_cached_lock_priority (lock);
  lock->holder = thread_current ();
  list_push_back (&lock->holder->list_of_locks, &lock->elem);
  cur->cached_priority = max (cur->cached_priority, lock->cached_priority);
  intr_set_level (old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  list_remove (&lock->elem); // this is fine since only the hold can remove
                             // the lock from acquired lock list
  lock->holder = NULL;       // also only the holder can change this
  struct thread *cur = thread_current ();

  enum intr_level old_level = intr_disable ();
  cur->cached_priority = recalc_cached_thread_priority (cur);
  sema_up (&lock->semaphore);
  intr_set_level (old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
{
  struct list_elem elem;      /* List element. */
  struct semaphore semaphore; /* This semaphore. */
  struct thread *holder;
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  waiter.holder = thread_current ();
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable ();
  if (!list_empty (&cond->waiters))
    {
      struct list_elem *e
          = list_max (&cond->waiters, less_sema_priority, NULL);
      list_remove (e);
      intr_set_level (old_level);
      sema_up (&list_entry (e, struct semaphore_elem, elem)->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

int
get_lock_priority (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (intr_get_level () == INTR_OFF);

  if (list_empty (&lock->semaphore.waiters))
    return 0;
  struct list_elem *e = list_max (&lock->semaphore.waiters,
                                  less_thread_effective_priority, NULL);
  struct thread *max_priority_thread = list_entry (e, struct thread, elem);
  return max_priority_thread->cached_priority;
}

bool
less_sema_priority (const struct list_elem *a, const struct list_elem *b,
                    void *aux UNUSED)
{
  struct semaphore_elem *s1 = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *s2 = list_entry (b, struct semaphore_elem, elem);

  return s1->holder->cached_priority < s2->holder->cached_priority;
}

// pre : intr off
static void
donate_lock_priority (struct lock *l, int new_priority)
{
  l->cached_priority = new_priority;
  if (new_priority > l->holder->cached_priority)
    donate_thread_priority (l->holder, new_priority);
}

// pre : intr off
static void
donate_thread_priority (struct thread *t, int new_priority)
{
  t->cached_priority = new_priority;
  if (t->lock_waiting != NULL
      && new_priority > t->lock_waiting->cached_priority)
    donate_lock_priority (t->lock_waiting, new_priority);
}

// pre : intr off
static int
recalc_cached_lock_priority (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (intr_get_level () == INTR_OFF);

  if (list_empty (&lock->semaphore.waiters))
    return 0;
  struct list_elem *e = list_max (&lock->semaphore.waiters,
                                  less_thread_effective_priority, NULL);
  struct thread *max_priority_thread = list_entry (e, struct thread, elem);
  return max_priority_thread->cached_priority;
}
#include "threads/thread.h"
#include "devices/timer.h"
#include "threads/fixed_point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define bound(x, low, high) (max (min ((x), (high)), (low)))

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
  void *eip;             /* Return address. */
  thread_func *function; /* Function to call. */
  void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-mlfqs". */
bool thread_mlfqs;

static fp_14 load_avg;
static struct list ready_queues[PRI_COUNT];
static int ready_queues_size;
static struct thread *threads_run_in_time_slice[TIME_SLICE];

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static void thread_tick_mlfqs (struct thread *t);
static int mlfqs_calc_priority (struct thread *t);
static void mlfqs_update_priority_reassign_queues (struct thread *t,
                                                   void *aux);
static void mlfqs_update_recent_cpu (struct thread *t, void *aux);
static void mlfqs_update_load_avg (void);
static void update_recent_cpu_and_priority (struct thread *t, void *aux);
static void mlfqs_push_ready_queues (struct thread *t);
static bool ready_queues_empty (void);
static struct list_elem *choose_thread_to_run_mlfqs (void);
static struct list_elem *choose_thread_to_run_donation (void);
static int highest_priority_in_ready_list (void);
static int mlfqs_highest_priority_in_ready_queue (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  if (thread_mlfqs)
    {
      load_avg = ntofp (0);
      for (int i = 0; i < PRI_COUNT; i++)
        {
          list_init (&ready_queues[i]);
        }
      ready_queues_size = 0;
    }

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Returns the number of threads currently in the ready list */
size_t
threads_ready (void)
{
  if (thread_mlfqs)
    {
      return ready_queues_size;
    }
  else
    {
      return list_size (&ready_list);
    }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if (thread_mlfqs)
    thread_tick_mlfqs (t);

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    {
      intr_yield_on_return ();
    }
}

/* On each timer tick, the running thread’s recent cpu is incremented by 1.

  Once per second:
  load avg is updated
  every thread’s recent cpu is updated
  priority for all threads is also updated since (100 % 4 == 0)

  every fourth clock tick
  Recalculate priority if necessary

  pre : intr_context()
*/
static void
thread_tick_mlfqs (struct thread *t)
{
  int slot = timer_ticks () % TIME_SLICE;

  threads_run_in_time_slice[slot] = t;

  // Update recent_cpu for current thread every tick
  if (t != idle_thread)
    t->recent_cpu = x_add_n (t->recent_cpu, 1);

  // per second
  if (timer_ticks () % TIMER_FREQ == 0)
    {
      // Update load_avg, recent_cpu, and thread priority every second
      mlfqs_update_load_avg ();
      thread_foreach (update_recent_cpu_and_priority, NULL);
    }
  // per 4 ticks
  else if (slot == 0)
    {
      // Update thread priority every 4th ticks
      // Because at most 4 threads' recent cpu is changed in a timer slice
      // so update at most 4 threads' recent cpu
      for (int i = 0; i < TIME_SLICE; i++)
        {
          struct thread *t = threads_run_in_time_slice[i];
          // if thread is exited, either t is not a thread
          // or t is allocated to a new thread
          // but that is fine, because update mlfqs priority won't change the
          // value
          if (!is_thread (t) || t == idle_thread)
            continue;
          mlfqs_update_priority_reassign_queues (t, NULL);
        }
    }
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* 1. create a thread

   2. if it has a higher priority than the running thread, switch to the new
   created thread

Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority, thread_func *function,
               void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack'
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  struct thread *cur = thread_current ();
  bool flag = thread_mlfqs ? t->priority > cur->priority
                           : (t->cached_priority > cur->cached_priority);

  ASSERT (!intr_context ());
  if (flag)
    thread_yield ();
  // pre of thread yield : !intr_context() but this function will not be called
  // in intr_context given : max_priority(ready_list) <= priority (running
  // thread) known : priority (waking thread) > priority (running thread)
  //                                  >= max_priority(ready_list)
  // thus we will always get the waking thread from ready list if it is the
  // highest prioritized thread

  intr_set_level (old_level);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* put waking thread into the ready list

  warning : doesn't yield! if necessary call thread yield

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{
  ASSERT (is_thread (t));

  enum intr_level old_level = intr_disable ();

  ASSERT (t->status == THREAD_BLOCKED);

  if (thread_mlfqs)
    {
      mlfqs_push_ready_queues (t);
    }
  else
    {
      list_push_back (&ready_list, &t->elem);
    }

  t->status = THREAD_READY;

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
  struct thread *t = running_thread ();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current ()->allelem);



  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Put the current thread into ready list (if current is not idle), and then
  get the first highest priorised thread from ready list to run.

  Set the status of current thread to ready.

  warning : if current thread is idle, you can still call it

  pre : !intr_context()
 Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    thread_mlfqs ? mlfqs_push_ready_queues (cur)
                 : list_push_back (&ready_list, &cur->elem);

  cur->status = THREAD_READY;
  // switch to the first highest priorised thread from ready list
  // if there is no thread in ready list, switch to idle
  schedule (); // pre : intr_off
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* pre : no effect in mlfqs

  If lowering its priority such that highest priority in ready list > new
priority switch to the first highest prioritized thread to run

Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  if (thread_mlfqs)
    return;
  struct thread *cur = thread_current ();
  cur->priority = new_priority;

  enum intr_level old_level = intr_disable ();

  cur->cached_priority = recalc_cached_thread_priority (cur);
  // because thread might be yielded, thus we cannot use a lock
  // to protect the ready list
  if (highest_priority_in_ready_list () > cur->cached_priority)
    intr_context () ? intr_yield_on_return () : thread_yield ();

  intr_set_level (old_level);
}

/*
pre : intr off
Returns the current thread's priority. */
int
thread_get_priority (void)
{
  return thread_current ()->cached_priority;
}

/* If the running thread no longer has the highest priority, yields.

Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
  ASSERT (nice >= -20 && nice <= 20);
  ASSERT (!intr_context ());

  struct thread *t = thread_current ();
  t->nice = nice;
  t->priority = mlfqs_calc_priority (t);

  enum intr_level old_level = intr_disable ();

  // because can only set the niceness of running thread
  // thus don't need to reassign ready queues
  // because thread yield might be called, cannot use locks, thus disable intr
  // Spec is ambiguous here, this is how we interpreted
  if (mlfqs_highest_priority_in_ready_queue () > t->priority)
    thread_yield (); // pre : !intr context

  intr_set_level (old_level);
}

/*Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
  return fpton_n (x_mul_n (load_avg, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  return fpton_n (x_mul_n (thread_current ()->recent_cpu, 100));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;)
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  ASSERT (function != NULL);

  intr_enable (); /* The scheduler runs with interrupts off. */
  function (aux); /* Execute the thread function. */
  thread_exit (); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0" : "=g"(esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

#ifdef USERPROG
  t->fd_incrementor = 3; // the first value is 3
                         // 0, 1, 2 for stdin stdout stderr
#endif // USERPROG

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *)t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_init (&t->list_of_locks);
  t->cached_priority = priority;

  list_init (&t->list_of_children);

  if (thread_mlfqs)
    {
      bool is_initial = strcmp (name, "main") == 0;
      struct thread *curr = running_thread ();
      t->nice = is_initial ? 0 : curr->nice;
      t->recent_cpu = is_initial ? 0 : curr->recent_cpu;
      t->priority = mlfqs_calc_priority (t);
    }

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* If      there is no ready threads returns idle
   Else    return the first highest prioritized thread in ready list

   pre : intr_off

Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
  ASSERT (intr_get_level () == INTR_OFF);
  // if (ready_threads() == 0)
  //  return idle_thread
  // else
  //  return poll_ready_list();

  if (threads_ready () == 0)
    { // pre : intr off
      return idle_thread;
    }
  else
    {
      struct list_elem *e = thread_mlfqs ? choose_thread_to_run_mlfqs ()
                                         : choose_thread_to_run_donation ();
      list_remove (e);
      return list_entry (e, struct thread, elem);
    }
}

static bool
ready_queues_empty (void)
{
  return ready_queues_size == 0;
}

static struct list_elem *
choose_thread_to_run_mlfqs (void)
{
  ASSERT (!ready_queues_empty ());
  ASSERT (intr_get_level () == INTR_OFF);

  int i = mlfqs_highest_priority_in_ready_queue ();
  ready_queues_size--;
  return list_front (ready_queues + i);
}

static int
mlfqs_highest_priority_in_ready_queue (void)
{
  if (threads_ready () == 0)
    return 0;
  int i = PRI_MAX;
  while (list_size (&ready_queues[i]) == 0)
    i--;
  return i;
}

inline static struct list_elem *
choose_thread_to_run_donation (void)
{
  return list_max (&ready_list, less_thread_effective_priority, NULL);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();

  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Find the highest prioritized thread in ready list or idle if no thread
   in ready list, then switch to that thread if it is different.

   If two threads has the same highest priority, then gives the first thread
   going into the ready list.

   pre : intr_off

  Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void)
{
  struct thread *cur = running_thread ();
  // Find the highest prioritized thread in ready list
  // if empty gives the idle thread
  // pre : intr_off
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next); // pre intr_off
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

// pre : disable intr
static bool
less_lock_priority (const struct list_elem *a, const struct list_elem *b,
                    void *aux UNUSED)
{
  ASSERT (intr_get_level () == INTR_OFF);
  struct lock *lock1 = list_entry (a, struct lock, elem);
  struct lock *lock2 = list_entry (b, struct lock, elem);

  return lock1->cached_priority < lock2->cached_priority;
}

// pre : disable intr
bool
less_thread_effective_priority (const struct list_elem *a,
                                const struct list_elem *b, void *aux UNUSED)
{
  ASSERT (intr_get_level () == INTR_OFF);
  struct thread *t1 = list_entry (a, struct thread, elem);
  struct thread *t2 = list_entry (b, struct thread, elem);

  int priority1 = t1->cached_priority;
  int priority2 = t2->cached_priority;

  return priority1 < priority2;
}

// pre : intr_context
static int
mlfqs_calc_priority (struct thread *t)
{
  int raw = PRI_MAX - fpton_n ((x_div_n (t->recent_cpu, 4))) - (t->nice * 2);
  return bound (raw, PRI_MIN, PRI_MAX);
}

// pre : intr_context
static void
mlfqs_update_recent_cpu (struct thread *t, void *aux UNUSED)
{
  ASSERT (timer_ticks () % TIMER_FREQ == 0);
  ASSERT (intr_context ());

  fp_14 k = x_mul_n (load_avg, 2);
  fp_14 coeff = x_div_y (k, x_add_n (k, 1));
  t->recent_cpu = x_add_n (x_mul_y (coeff, t->recent_cpu), t->nice);
}

// pre : intr_context
static void
mlfqs_update_load_avg (void)
{
  ASSERT (timer_ticks () % TIMER_FREQ == 0);
  ASSERT (intr_context ());

  fp_14 fst = x_mul_y (x_div_n (ntofp (59), 60), load_avg);
  fp_14 snd = x_mul_n (x_div_n (ntofp (1), 60),
                       ready_queues_size + (thread_current () != idle_thread));
  load_avg = x_add_y (fst, snd);
}

// pre : intr_context
/* update mlfqs priortiy in t, reassign queue if ready_thread_priority changed
 */
static void
mlfqs_update_priority_reassign_queues (struct thread *t, void *aux UNUSED)
{
  ASSERT (intr_context ());
  int old_priority = t->priority;
  t->priority = mlfqs_calc_priority (t);

  if (t->priority != old_priority && t->status == THREAD_READY
      && t != idle_thread)
    {
      list_remove (&t->elem);
      list_push_back (&ready_queues[t->priority], &t->elem);
    }
}

// pre : intr_context
static void
update_recent_cpu_and_priority (struct thread *t, void *aux UNUSED)
{
  mlfqs_update_recent_cpu (t, aux);
  mlfqs_update_priority_reassign_queues (t, aux);
}

/* pre : disable intr || lock

Update priority of thread and assign it to one of the 64 ready queues */
static void
mlfqs_push_ready_queues (struct thread *t)
{

  int mlfqs_priority = mlfqs_calc_priority (t);
  t->priority = mlfqs_priority;
  enum intr_level old_level = intr_disable ();
  list_push_back (&ready_queues[mlfqs_priority], &t->elem);
  ready_queues_size++;
  intr_set_level (old_level);
}

// pre : ready_list lock || disable intr
static int
highest_priority_in_ready_list ()
{
  if (threads_ready () == 0)
    return 0;
  struct list_elem *e
      = list_max (&ready_list, less_thread_effective_priority, NULL);
  return list_entry (e, struct thread, elem)->cached_priority;
}

// pre : intr_off
int
recalc_cached_thread_priority (struct thread *t)
{
  ASSERT (t != NULL);
  ASSERT (intr_get_level () == INTR_OFF);
  if (list_empty (&t->list_of_locks))
    return t->priority;
  struct lock *max_priority_lock
      = list_entry (list_max (&t->list_of_locks, less_lock_priority, NULL),
                    struct lock, elem);
  int lock_priority = max_priority_lock->cached_priority;
  return max (t->priority, lock_priority);
}

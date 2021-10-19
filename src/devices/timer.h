#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include <list.h>

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

/*
 * The structure helps to deal with threads that
 * wakes up after WAKE_UP_TICKS number of ticks.
 */
struct alarm_clock_helper {
    int64_t wake_up_ticks;
    /* Can use the semaphore to put the thread
     * to sleep and unblock the thread
     */
    struct semaphore *timer_sema;
    struct list_elem elem; /* Can be used by list_entry to track the address */
};


void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);


/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

#endif /* devices/timer.h */

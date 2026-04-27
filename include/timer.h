#ifndef TIMER_H
#define TIMER_H

#include <pthread.h>

extern volatile int    global_tick;
extern volatile int    simulation_running;
extern pthread_mutex_t tick_lock;
extern pthread_cond_t  tick_changed;
extern int             tick_interval_ms;

void  timer_init(void);
void  timer_destroy(void);
void *timer_thread(void *arg);
void  wait_until_tick(int target_tick);

#endif // TIMER_H 
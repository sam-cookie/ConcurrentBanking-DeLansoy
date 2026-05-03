#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "timer.h"

volatile int    global_tick = 0;
volatile int    simulation_running = 1;
pthread_mutex_t tick_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tick_changed = PTHREAD_COND_INITIALIZER;
int             tick_interval_ms = 100;

void timer_init(void)
{
    if (pthread_mutex_init(&tick_lock, NULL) != 0) {
        perror("timer_init: pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&tick_changed, NULL) != 0) {
        perror("timer_init: pthread_cond_init");
        exit(EXIT_FAILURE);
    }
}

void timer_destroy(void)
{
    pthread_cond_destroy(&tick_changed);
    pthread_mutex_destroy(&tick_lock);
}

void *timer_thread(void *arg)
{
    (void)arg;

    while (simulation_running) {
        usleep(tick_interval_ms * 1000);

        pthread_mutex_lock(&tick_lock);
        global_tick++;
        pthread_cond_broadcast(&tick_changed);
        pthread_mutex_unlock(&tick_lock);
    }

    return NULL;
}

void wait_until_tick(int target_tick)
{
    pthread_mutex_lock(&tick_lock);
    while (global_tick < target_tick) {
        pthread_cond_wait(&tick_changed, &tick_lock);
    }
    pthread_mutex_unlock(&tick_lock);
}
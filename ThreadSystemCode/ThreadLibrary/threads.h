#ifndef UTHREAD_THREADS_H
#define UTHREAD_THREADS_H

#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lock_t *Lock;
typedef struct Condition_t *Condition;
typedef struct Semaphore_t *Semaphore;

void t_start(ThreadFunc function, any_ptr argument, const char *name, int priority);
int t_fork(ThreadFunc function, any_ptr argument, const char *name, int priority);
void t_join(int thread_id);
void t_yield(void);
void t_exit(int value);
int t_priority(void);
char *getThreadName(void);

void t_set_quantum(long milliseconds);
void t_set_system_quantum(int microseconds);

Lock lock_create(void);
void lock_acquire(Lock lock);
void lock_release(Lock lock);
void lock_destroy(Lock lock);

Condition cond_create(Lock lock);
void cond_destroy(Condition condition);
any_ptr t_wait(Condition condition, Lock lock);
void t_sig(Condition condition, any_ptr value, Lock lock);

Semaphore semaphore_create(int count, const char *name);
void Semaphore_P(Semaphore semaphore);
void Semaphore_V(Semaphore semaphore);
void Semaphore_destroy(Semaphore semaphore);

/* Compatibility aliases used by early clients of the library. */
#define acquire lock_acquire
#define release lock_release

#ifdef __cplusplus
}
#endif

#endif

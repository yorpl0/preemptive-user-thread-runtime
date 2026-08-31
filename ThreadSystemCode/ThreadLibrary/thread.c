#define _XOPEN_SOURCE 700

#include "threads.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>

enum {
    MIN_PRIORITY = 0,
    MAX_PRIORITY = 127,
    MAX_THREADS = 4096,
    STACK_SIZE = 64 * 1024,
    DEFAULT_QUANTUM_US = 10000
};

typedef enum ThreadState {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_FINISHED
} ThreadState;

typedef struct Thread Thread;

typedef struct ThreadQueue {
    Thread *head;
    Thread *tail;
} ThreadQueue;

struct Thread {
    int id;
    int priority;
    int exit_value;
    char name[32];
    ThreadState state;
    ThreadFunc function;
    any_ptr argument;
    any_ptr signal_value;
    void *stack;
    ucontext_t context;
    ThreadQueue joiners;
    Thread *next;
};

struct Lock_t {
    Thread *owner;
    ThreadQueue waiters[MAX_PRIORITY + 1];
};

struct Condition_t {
    Lock associated_lock;
    ThreadQueue waiters[MAX_PRIORITY + 1];
};

struct Semaphore_t {
    int value;
    char name[32];
    ThreadQueue waiters[MAX_PRIORITY + 1];
};

typedef struct Runtime {
    ThreadQueue ready[MAX_PRIORITY + 1];
    Thread *threads[MAX_THREADS];
    Thread *current;
    ucontext_t scheduler;
    struct sigaction previous_action;
    struct itimerval previous_timer;
    int next_id;
    int live_threads;
    int quantum_us;
    volatile sig_atomic_t running;
} Runtime;

static Runtime runtime = { .quantum_us = DEFAULT_QUANTUM_US };

static int valid_priority(int priority)
{
    return priority >= MIN_PRIORITY && priority <= MAX_PRIORITY;
}

static void queue_push(ThreadQueue *queue, Thread *thread)
{
    thread->next = NULL;
    if (queue->tail) {
        queue->tail->next = thread;
    } else {
        queue->head = thread;
    }
    queue->tail = thread;
}

static Thread *queue_pop(ThreadQueue *queue)
{
    Thread *thread = queue->head;
    if (!thread) {
        return NULL;
    }
    queue->head = thread->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    thread->next = NULL;
    return thread;
}

static int priority_queues_empty(ThreadQueue queues[])
{
    for (int priority = MAX_PRIORITY; priority >= MIN_PRIORITY; --priority) {
        if (queues[priority].head) {
            return 0;
        }
    }
    return 1;
}

static Thread *priority_pop(ThreadQueue queues[])
{
    for (int priority = MAX_PRIORITY; priority >= MIN_PRIORITY; --priority) {
        Thread *thread = queue_pop(&queues[priority]);
        if (thread) {
            return thread;
        }
    }
    return NULL;
}

static void make_ready(Thread *thread)
{
    if (!thread || thread->state == THREAD_FINISHED) {
        return;
    }
    thread->state = THREAD_READY;
    queue_push(&runtime.ready[thread->priority], thread);
}

static void block_timer(sigset_t *previous_mask)
{
    sigset_t timer_signal;
    sigemptyset(&timer_signal);
    sigaddset(&timer_signal, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &timer_signal, previous_mask);
}

static void restore_mask(const sigset_t *previous_mask)
{
    sigprocmask(SIG_SETMASK, previous_mask, NULL);
}

static void arm_timer(void)
{
    struct itimerval timer = {0};
    int quantum = runtime.quantum_us > 0 ? runtime.quantum_us : DEFAULT_QUANTUM_US;
    timer.it_value.tv_sec = quantum / 1000000;
    timer.it_value.tv_usec = quantum % 1000000;
    timer.it_interval = timer.it_value;
    setitimer(ITIMER_VIRTUAL, &timer, NULL);
}

static void suspend_current(ThreadQueue *waiters)
{
    Thread *current = runtime.current;
    current->state = THREAD_BLOCKED;
    queue_push(waiters, current);
    swapcontext(&current->context, &runtime.scheduler);
}

static void thread_bootstrap(int id)
{
    Thread *thread = runtime.threads[id];
    thread->function(thread->argument);
    t_exit(0);
}

static int spawn_thread(ThreadFunc function, any_ptr argument,
                        const char *name, int priority)
{
    if (!function || !valid_priority(priority) || runtime.next_id >= MAX_THREADS) {
        return -1;
    }

    Thread *thread = calloc(1, sizeof(*thread));
    if (!thread) {
        return -1;
    }
    thread->stack = malloc(STACK_SIZE);
    if (!thread->stack) {
        free(thread);
        return -1;
    }

    thread->id = runtime.next_id++;
    thread->priority = priority;
    thread->state = THREAD_READY;
    thread->function = function;
    thread->argument = argument;
    snprintf(thread->name, sizeof(thread->name), "%s", name ? name : "thread");

    if (getcontext(&thread->context) == -1) {
        free(thread->stack);
        free(thread);
        return -1;
    }
    thread->context.uc_stack.ss_sp = thread->stack;
    thread->context.uc_stack.ss_size = STACK_SIZE;
    thread->context.uc_link = &runtime.scheduler;
    sigemptyset(&thread->context.uc_sigmask);
    makecontext(&thread->context, (void (*)(void))thread_bootstrap, 1, thread->id);

    runtime.threads[thread->id] = thread;
    runtime.live_threads++;
    make_ready(thread);
    return thread->id;
}

static void timer_handler(int signal_number)
{
    (void)signal_number;
    Thread *current = runtime.current;
    if (!runtime.running || !current || priority_queues_empty(runtime.ready)) {
        return;
    }
    make_ready(current);
    swapcontext(&current->context, &runtime.scheduler);
}

static void scheduler_loop(void)
{
    Thread *next;
    while ((next = priority_pop(runtime.ready)) != NULL) {
        runtime.current = next;
        next->state = THREAD_RUNNING;
        swapcontext(&runtime.scheduler, &next->context);

        if (next->state == THREAD_FINISHED && next->stack) {
            free(next->stack);
            next->stack = NULL;
        }
        runtime.current = NULL;
    }

    if (runtime.live_threads > 0) {
        fprintf(stderr, "uthread: deadlock; %d thread(s) remain blocked\n",
                runtime.live_threads);
    }
}

static void release_lock_internal(Lock lock)
{
    if (!lock || lock->owner != runtime.current) {
        return;
    }
    Thread *next_owner = priority_pop(lock->waiters);
    lock->owner = next_owner;
    make_ready(next_owner);
}

static void clean_runtime(void)
{
    for (int id = 1; id < runtime.next_id; ++id) {
        Thread *thread = runtime.threads[id];
        if (thread) {
            free(thread->stack);
            free(thread);
        }
    }
    int quantum = runtime.quantum_us;
    memset(&runtime, 0, sizeof(runtime));
    runtime.quantum_us = quantum;
}

void t_start(ThreadFunc function, any_ptr argument, const char *name, int priority)
{
    if (runtime.running || !function || !valid_priority(priority)) {
        return;
    }

    sigset_t previous_mask;
    block_timer(&previous_mask);
    runtime.running = 1;
    runtime.next_id = 1;
    getcontext(&runtime.scheduler);

    struct sigaction action = {0};
    action.sa_handler = timer_handler;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGVTALRM, NULL, &runtime.previous_action);
    getitimer(ITIMER_VIRTUAL, &runtime.previous_timer);
    sigaction(SIGVTALRM, &action, NULL);

    if (spawn_thread(function, argument, name, priority) >= 0) {
        arm_timer();
        scheduler_loop();
    }

    struct itimerval stopped = {0};
    setitimer(ITIMER_VIRTUAL, &stopped, NULL);
    sigaction(SIGVTALRM, &runtime.previous_action, NULL);
    setitimer(ITIMER_VIRTUAL, &runtime.previous_timer, NULL);
    runtime.running = 0;
    clean_runtime();
    restore_mask(&previous_mask);
}

int t_fork(ThreadFunc function, any_ptr argument, const char *name, int priority)
{
    if (!runtime.running || !runtime.current) {
        return -1;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    int id = spawn_thread(function, argument, name, priority);
    if (id >= 0 && priority > runtime.current->priority) {
        Thread *current = runtime.current;
        make_ready(current);
        swapcontext(&current->context, &runtime.scheduler);
    }
    restore_mask(&previous_mask);
    return id;
}

void t_join(int thread_id)
{
    if (!runtime.current || thread_id <= 0 || thread_id >= runtime.next_id) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    Thread *target = runtime.threads[thread_id];
    if (target && target != runtime.current && target->state != THREAD_FINISHED) {
        suspend_current(&target->joiners);
    }
    restore_mask(&previous_mask);
}

void t_yield(void)
{
    if (!runtime.current) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    if (!priority_queues_empty(runtime.ready)) {
        Thread *current = runtime.current;
        make_ready(current);
        swapcontext(&current->context, &runtime.scheduler);
    }
    restore_mask(&previous_mask);
}

void t_exit(int value)
{
    if (!runtime.current) {
        return;
    }
    sigset_t ignored;
    block_timer(&ignored);
    Thread *current = runtime.current;
    current->exit_value = value;
    current->state = THREAD_FINISHED;
    runtime.live_threads--;
    Thread *joiner;
    while ((joiner = queue_pop(&current->joiners)) != NULL) {
        make_ready(joiner);
    }
    setcontext(&runtime.scheduler);
    abort();
}

int t_priority(void)
{
    return runtime.current ? runtime.current->priority : -1;
}

char *getThreadName(void)
{
    return runtime.current ? runtime.current->name : NULL;
}

void t_set_system_quantum(int microseconds)
{
    if (microseconds <= 0) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    runtime.quantum_us = microseconds;
    if (runtime.running) {
        arm_timer();
    }
    restore_mask(&previous_mask);
}

void t_set_quantum(long milliseconds)
{
    if (milliseconds > 0 && milliseconds <= INT32_MAX / 1000) {
        t_set_system_quantum((int)(milliseconds * 1000));
    }
}

Lock lock_create(void)
{
    return calloc(1, sizeof(struct Lock_t));
}

void lock_acquire(Lock lock)
{
    if (!lock || !runtime.current) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    if (!lock->owner) {
        lock->owner = runtime.current;
    } else if (lock->owner != runtime.current) {
        suspend_current(&lock->waiters[runtime.current->priority]);
    }
    restore_mask(&previous_mask);
}

void lock_release(Lock lock)
{
    sigset_t previous_mask;
    block_timer(&previous_mask);
    release_lock_internal(lock);
    restore_mask(&previous_mask);
}

void lock_destroy(Lock lock)
{
    if (lock && !lock->owner && priority_queues_empty(lock->waiters)) {
        free(lock);
    }
}

Condition cond_create(Lock lock)
{
    Condition condition = calloc(1, sizeof(*condition));
    if (condition) {
        condition->associated_lock = lock;
    }
    return condition;
}

void cond_destroy(Condition condition)
{
    if (condition && priority_queues_empty(condition->waiters)) {
        free(condition);
    }
}

any_ptr t_wait(Condition condition, Lock lock)
{
    if (!condition || !runtime.current) {
        return NULL;
    }
    Lock held_lock = lock ? lock : condition->associated_lock;
    sigset_t previous_mask;
    block_timer(&previous_mask);
    Thread *current = runtime.current;
    current->signal_value = NULL;
    release_lock_internal(held_lock);
    suspend_current(&condition->waiters[current->priority]);
    any_ptr value = current->signal_value;
    restore_mask(&previous_mask);
    if (held_lock) {
        lock_acquire(held_lock);
    }
    return value;
}

void t_sig(Condition condition, any_ptr value, Lock lock)
{
    (void)lock;
    if (!condition) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    Thread *waiter = priority_pop(condition->waiters);
    if (waiter) {
        waiter->signal_value = value;
        make_ready(waiter);
    }
    restore_mask(&previous_mask);
}

Semaphore semaphore_create(int count, const char *name)
{
    if (count < 0) {
        return NULL;
    }
    Semaphore semaphore = calloc(1, sizeof(*semaphore));
    if (semaphore) {
        semaphore->value = count;
        snprintf(semaphore->name, sizeof(semaphore->name), "%s",
                 name ? name : "semaphore");
    }
    return semaphore;
}

void Semaphore_P(Semaphore semaphore)
{
    if (!semaphore || !runtime.current) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    if (semaphore->value > 0) {
        semaphore->value--;
    } else {
        suspend_current(&semaphore->waiters[runtime.current->priority]);
    }
    restore_mask(&previous_mask);
}

void Semaphore_V(Semaphore semaphore)
{
    if (!semaphore) {
        return;
    }
    sigset_t previous_mask;
    block_timer(&previous_mask);
    Thread *waiter = priority_pop(semaphore->waiters);
    if (waiter) {
        make_ready(waiter);
    } else {
        semaphore->value++;
    }
    restore_mask(&previous_mask);
}

void Semaphore_destroy(Semaphore semaphore)
{
    if (semaphore && priority_queues_empty(semaphore->waiters)) {
        free(semaphore);
    }
}

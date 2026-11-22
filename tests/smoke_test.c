#include "queue.h"
#include "threads.h"

#include <stdio.h>
#include <string.h>

static char trace[32];
static size_t trace_length;
static Semaphore gate;
static Lock condition_lock;
static Condition condition;
static int condition_ready;

static void mark(char event)
{
    trace[trace_length++] = event;
    trace[trace_length] = '\0';
}

static void low_priority(any_ptr unused)
{
    (void)unused;
    mark('D');
}

static void high_priority(any_ptr unused)
{
    (void)unused;
    mark('B');
}

static void semaphore_waiter(any_ptr unused)
{
    (void)unused;
    Semaphore_P(gate);
    mark('G');
}

static void condition_waiter(any_ptr unused)
{
    (void)unused;
    lock_acquire(condition_lock);
    while (!condition_ready) {
        t_wait(condition, condition_lock);
    }
    mark('J');
    lock_release(condition_lock);
}

static void root_thread(any_ptr unused)
{
    (void)unused;
    mark('A');
    int low_id = t_fork(low_priority, NULL, "low", 5);
    t_fork(high_priority, NULL, "high", 20);
    mark('C');
    t_join(low_id);
    mark('E');

    t_fork(semaphore_waiter, NULL, "semaphore-waiter", 20);
    mark('F');
    Semaphore_V(gate);
    t_yield();
    mark('H');

    t_fork(condition_waiter, NULL, "condition-waiter", 20);
    lock_acquire(condition_lock);
    condition_ready = 1;
    t_sig(condition, NULL, condition_lock);
    lock_release(condition_lock);
    mark('I');
    t_yield();
    mark('K');
}

int main(void)
{
    Queue queue = q_create("smoke");
    int first = 1;
    int second = 2;
    q_insert(queue, &first);
    q_insert(queue, &second);
    if (q_size(queue) != 2 || q_remove(queue) != &first ||
        q_remove(queue) != &second || q_remove(queue) != NULL) {
        return 1;
    }
    q_destroy(queue);

    gate = semaphore_create(0, "gate");
    condition_lock = lock_create();
    condition = cond_create(condition_lock);
    t_start(root_thread, NULL, "root", 10);

    Semaphore_destroy(gate);
    cond_destroy(condition);
    lock_destroy(condition_lock);

    if (strcmp(trace, "ABCDEFGHIJK") != 0) {
        fprintf(stderr, "unexpected schedule: %s\n", trace);
        return 1;
    }
    puts("thread runtime smoke test passed");
    return 0;
}

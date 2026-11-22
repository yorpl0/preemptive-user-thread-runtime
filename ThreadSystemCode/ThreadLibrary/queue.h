#ifndef UTHREAD_QUEUE_H
#define UTHREAD_QUEUE_H

#include <stddef.h>

typedef void *any_ptr;
typedef int Boolean;

enum { FALSE = 0, TRUE = 1 };

typedef struct Queue_t *Queue;
typedef void (*ThreadFunc)(any_ptr);

Queue q_create(const char *name);
void q_destroy(Queue queue);
void q_insert(Queue queue, any_ptr value);
any_ptr q_remove(Queue queue);
Boolean q_is_empty(Queue queue);
size_t q_size(Queue queue);

#endif

#include "queue.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct QueueNode {
    any_ptr value;
    struct QueueNode *next;
} QueueNode;

struct Queue_t {
    char name[32];
    size_t size;
    QueueNode *head;
    QueueNode *tail;
};

Queue q_create(const char *name)
{
    Queue queue = calloc(1, sizeof(*queue));
    if (queue && name) {
        snprintf(queue->name, sizeof(queue->name), "%s", name);
    }
    return queue;
}

void q_destroy(Queue queue)
{
    if (!queue) {
        return;
    }
    while (queue->head) {
        q_remove(queue);
    }
    free(queue);
}

void q_insert(Queue queue, any_ptr value)
{
    if (!queue) {
        return;
    }
    QueueNode *node = malloc(sizeof(*node));
    if (!node) {
        return;
    }
    node->value = value;
    node->next = NULL;
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->size++;
}

any_ptr q_remove(Queue queue)
{
    if (!queue || !queue->head) {
        return NULL;
    }
    QueueNode *node = queue->head;
    any_ptr value = node->value;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->size--;
    free(node);
    return value;
}

Boolean q_is_empty(Queue queue)
{
    return !queue || queue->size == 0;
}

size_t q_size(Queue queue)
{
    return queue ? queue->size : 0;
}

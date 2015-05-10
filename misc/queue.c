/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: queue.c
 * @author: Steven R. Emmerson
 *
 * This file implements a simple queue of pointers. The queue is
 * thread-compatible but not thread-safe.
 */

#include "config.h"

#include "queue.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct qElt QElt;

struct qElt {
    void* ptr;
    QElt* next;
};

struct queue {
    QElt*  head;
    QElt*  tail;
    size_t count;
};

Queue* q_new(void)
{
    Queue* const q = malloc(sizeof(Queue));

    if (q) {
        q->head = q->tail = NULL;
        q->count = 0;
    }

    return q;
}

/**
 * Enqueues an element at the tail-end of a queue.
 *
 * @param[in] q       The queue.
 * @param[in] ptr     The pointer to be enqueued.
 * @retval    0       Success.
 * @retval    ENOMEM  Out-of-memory.
 */
int q_enqueue(
        Queue* const restrict q,
        void* const restrict  ptr)
{
    QElt* newTail = malloc(sizeof(QElt));
    int   status;

    if (newTail == NULL) {
        status = ENOMEM;
    }
    else {
        newTail->ptr = ptr;
        newTail->next = NULL;

        if (q->tail) {
            q->tail->next = newTail;
        }
        else {
            q->head = newTail;
        }
        q->tail = newTail;
        q->count++;

        status = 0;
    }

    return status;
}

void* q_dequeue(
        Queue* const restrict q)
{
    void* ptr;

    if (q->head == NULL) {
        ptr = NULL;
    }
    else {
        QElt* oldHead = q->head;
        q->head = oldHead->next;
        if (q->head == NULL)
            q->tail = NULL;
        ptr = oldHead->ptr;
        free(oldHead);
        q->count--;
    }

    return ptr;
}

size_t q_size(
        Queue* const restrict q)
{
    return q->count;
}

void q_free(
        Queue* const q)
{
    if (q) {
        for (QElt* elt = q->head; elt; ) {
            QElt* next = elt->next;
            free(elt);
            elt = next;
        }
        free(q);
    }
}

/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file doubly_linked_list.c
 *
 * This file implements a thread-compatible but not thread-safe doubly-linked
 * FIFO list of arbitrary (but non-NULL) pointers.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "doubly_linked_list.h"
#include "log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

struct DllElt {
    DllElt* prev;
    DllElt* next;
    void*   ptr;
};
struct Dll {
    DllElt* head;
    DllElt* tail;
    size_t  size;
};
struct DllIter {
    DllElt* elt;
};

/**
 * Returns a new doubly-linked list.
 *
 * @retval NULL  Error. `log_add()` called.
 * @return       Pointer to a new doubly-linked list. The client should call
 *               `dll_free()` when it is no longer needed.
 */
Dll*
dll_new(void)
{
    Dll* dll = LOG_MALLOC(sizeof(Dll), "doubly-linked list");

    if (dll) {
        dll->tail = dll->head = NULL;
        dll->size = 0;
    }

    return dll;
}

/**
 * Adds a pointer to the tail of a doubly-linked list.
 *
 * @param[in] dll   Pointer to the list to have a pointer added to it.
 * @param[in] ptr   The pointer to be added. Should not be `NULL`.
 * @retval    NULL  Out-of-memory. `log_add()` called.
 * @retval    NULL  `ptr == NULL`. `log_add()` called.
 * @return          Pointer to the list element that contains the pointer.
 */
DllElt*
dll_add(
    Dll* const  dll,
    void* const ptr)
{
    DllElt* elt;

    if (ptr == NULL) {
        LOG_START0("Null pointer");
        elt = NULL;
    }
    else {
        elt = LOG_MALLOC(sizeof(DllElt), "doubly-linked list element");

        if (elt) {
            elt->ptr = ptr;
            elt->next = NULL;
            elt->prev = dll->tail;
            if (dll->head == NULL)
                dll->head = elt;
            if (dll->tail)
                dll->tail->next = elt;
            dll->tail = elt;
            dll->size++;
        }
    }

    return elt;
}

/**
 * Removes and returns the pointer at the head of a doubly-linked list.
 *
 * @param[in] dll   Pointer to the doubly-linked list.
 * @return          The pointer that the head element contained.
 * @retval    NULL  The list is empty.
 */
void*
dll_getFirst(
    Dll* const dll)
{
    void*         ptr;
    DllElt* const first = dll->head;

    if (first == NULL) {
        ptr = NULL;
    }
    else {
        if (first->next)
            first->next->prev = NULL;
        dll->head = first->next;
        if (dll->tail == first)
            dll->tail = NULL;
        dll->size--;

        ptr = first->ptr;
        free(first);
    }

    return ptr;
}

/**
 * Returns an iterator.
 *
 * @param[in] dll   The doubly-linked-list.
 * @retval    NULL  Out-of-memory. `log_add()` called.
 * @return          An iterator over the list.
 */
DllIter* dll_iter(
        Dll* const dll)
{
    DllIter* const iter = LOG_MALLOC(sizeof(DllIter),
            "doubly-linked-list iterator");

    if (iter)
        iter->elt = dll->head; // might be `NULL`

    return iter;
}

/**
 * Indicates if the next pointer exists.
 *
 * @param[in] iter    The iterator.
 * @retval    `true`  if and only if the next pointer exits.
 */
bool dll_hasNext(
        DllIter* const iter)
{
    return iter->elt != NULL;
}

/**
 * Returns the next pointer.
 *
 * @pre             `dll_hasNext(iter) == true`
 * @param[in] iter  The iterator.
 * @return          The next pointer.
 */
void* dll_next(
        DllIter* const iter)
{
    void *ptr = iter->elt->ptr;
    iter->elt = iter->elt->next;
    return ptr;
}

/**
 * Frees an iterator.
 *
 * @param[in] iter  The iterator to be freed.
 */
void dll_freeIter(
        DllIter* const iter)
{
    free(iter);
}

/**
 * Returns the number of elements in a doubly-linked list.
 *
 * @param[in] dll  Pointer to the doubly-linked list.
 * @return         The number of elements in the list.
 */
size_t
dll_size(
    const Dll* const dll)
{
    return dll->size;
}

/**
 * Removes an element from a doubly-linked list.
 *
 * @param[in] dll  Pointer to the list to have an element removed.
 * @param[in] elt  Pointer to the element to be removed. Must have been returned
 *                 by `dll_add()` and must be in the list. The caller must not
 *                 reference `elt` after this function.
 * @return         The pointer that the removed element contained.
 */
void*
dll_remove(
    Dll* const    dll,
    DllElt* const elt)
{
    DllElt* prev = elt->prev;
    DllElt* next = elt->next;

    if (prev) {
        prev->next = next;
    }
    else {
        dll->head = next;
    }

    if (next) {
        next->prev = prev;
    }
    else {
        dll->tail = prev;
    }

    dll->size--;

    void* ptr = elt->ptr;
    free(elt);

    return ptr;
}

/**
 * Frees a doubly-linked list.
 *
 * @param[in] dll  Pointer to the doubly-linked list to be freed. Must have
 *                 been returned by `dll_new()`.
 */
void
dll_free(
    Dll* const dll)
{
    DllElt* elt = dll->head;

    while (elt != NULL) {
        DllElt* next = elt->next;

        free(elt);
        elt = next;
    }

    free(dll);
}

/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file doubly_linked_list.c
 *
 * This file implements a thread-compatible but not thread-safe doubly-linked
 * FIFO list of arbitrary pointers.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "doubly_linked_list.h"
#include "log.h"

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
 * @retval    NULL  Error. `log_add()` called.
 * @return          Pointer to the list element that contains the pointer.
 */
DllElt*
dll_add(
    Dll* const  dll,
    void* const ptr)
{
    DllElt* elt = LOG_MALLOC(sizeof(DllElt), "doubly-linked list element");

    if (elt) {
        elt->next = NULL;
        elt->prev = dll->tail;
        dll->tail = elt;
        if (elt->prev)
            elt->prev->next = elt;
        if (dll->head == NULL)
            dll->head = elt;
        dll->size++;
    }

    return elt;
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
 * Returns the pointer at the head of a doubly-linked list.
 *
 * @param[in] dll   Pointer to the doubly-linked list.
 * @return          The pointer that the head element contained.
 * @retval    NULL  The list is empty.
 */
void*
dll_getFirst(
    Dll* const dll)
{
    DllElt* const first = dll->head;

    if (first == NULL)
        return NULL;

    dll->head = first->next;
    if (dll->head)
        dll->head->prev = NULL;
    if (dll->tail == first)
        dll->tail = NULL;
    dll->size--;

    return first->ptr;
}

/**
 * Removes an element from a doubly-linked list.
 *
 * @param[in] dll  Pointer to the list to have an element removed.
 * @param[in] elt  Pointer to the element to be removed. Must have been returned
 *                 by `dll_push(dll)` and must be in the list.
 * @return         The pointer that the removed element contained.
 */
void*
dll_remove(
    Dll* const    dll,
    DllElt* const elt)
{
    DllElt* prev = elt->prev;
    DllElt* next = elt->next;

    if (prev)
        prev->next = elt->next;

    if (next)
        next->prev = elt->prev;

    return elt->ptr;
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

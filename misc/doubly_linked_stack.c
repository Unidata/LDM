/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file doubly_linked_stack.c
 *
 * This file implements a thread-compatible but not thread-safe stack of
 * arbitrary pointers. It is implemented using a doubly-linked list in order to
 * support additional functions.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "doubly_linked_stack.h"
#include "mylog.h"

#include <stdlib.h>

struct DlsElt {
    void*   ptr;
    DlsElt* up;
    DlsElt* down;
    // TODO
};
struct Dls {
    DlsElt* top;
    // TODO
};

/**
 * Returns a new doubly-linked stack.
 *
 * @retval NULL  Error. `mylog_add()` called.
 * @return       Pointer to a new doubly-linked stack. The client should call
 *               `dls_free()` when it is no longer needed.
 */
Dls*
dls_new(void)
{
    Dls* dls = mylog_malloc(sizeof(Dls), "doubly-linked stack");

    if (dls)
        dls->top = NULL;

    return dls;
}

/**
 * Pushes a pointer onto a doubly-linked stack.
 *
 * @param[in] dls   Pointer to the stack to have a pointer pushed onto it.
 * @param[in] ptr   The pointer to be pushed. Should not be `NULL`.
 * @retval    NULL  Error. `mylog_add()` called.
 * @return          Pointer to the stack element that contains the pointer.
 */
DlsElt*
dls_push(
    Dls* const  dls,
    void* const ptr)
{
    DlsElt* elt = mylog_malloc(sizeof(DlsElt), "doubly-linked stack element");

    if (elt) {
        elt->down = dls->top;
        dls->top = dls->top->up = elt;
    }

    return elt;
}

/**
 * Pops a pointer from a doubly-linked stack.
 *
 * @param[in] dls   Pointer to the stack to be popped.
 * @return          The pointer that the popped element contained.
 * @retval    NULL  The stack is empty.
 */
void*
dls_pop(
    Dls* const dls)
{
    DlsElt* const elt = dls->top;

    if (elt == NULL)
        return NULL;

    dls->top = elt->down;

    return elt->ptr;
}

/**
 * Removes an element from a doubly-linked stack.
 *
 * @param[in] dls  Pointer to the stack to have an element removed.
 * @param[in] elt  Pointer to the element to be removed. Must have been returned
 *                 by `dls_push(dls)` and must be in the stack.
 * @return         The pointer that the removed element contained.
 */
void*
dls_remove(
    Dls* const    dls,
    DlsElt* const elt)
{
    DlsElt* up = elt->up;
    DlsElt* down = elt->down;

    if (up)
        up->down = elt->down;

    if (down)
        down->up = elt->up;

    return elt->ptr;
}

/**
 * Frees a doubly-linked stack.
 *
 * @param[in] dls  Pointer to the doubly-linked stack to be freed. Must have
 *                 been returned by `dls_new()`.
 */
void
dls_free(
    Dls* const dls)
{
    DlsElt* elt = dls->top;

    while (elt != NULL) {
        DlsElt* down = elt->down;

        free(elt);
        elt = down;
    }

    free(dls);
}

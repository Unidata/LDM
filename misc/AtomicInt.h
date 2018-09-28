/**
 * This file defines an integer that can be accessed atomically.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: AtomicInt.h
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#ifndef MISC_ATOMIC_INT_H_
#define MISC_ATOMIC_INT_H_

typedef struct atomicInt AtomicInt;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new atomic integer.
 *
 * @param[in]  initVal      Initial value
 * @retval     NULL         Failure. `log_add()` called.
 * @return                  New atomic integer
 * @asyncsignalsafety       Unsafe
 */
AtomicInt*
atomicInt_new(const int initVal);

/**
 * Frees an atomic integer.
 *
 * @param[in,out] atomicInt  Atomic integer to be freed or `NULL`
 *
 * @asyncsignalsafety         Unsafe
 */
void
atomicInt_free(AtomicInt* const atomicInt);

/**
 * Sets an atomic integer.
 *
 * @param[in,out] atomicInt  Atomic integer to be set
 * @param[in]     newVal     New value for atomic integer
 * @return                   Previous value of atomic integer
 * @asyncsignalsafety        Unsafe
 */
int
atomicInt_set(
        AtomicInt* const atomicInt,
        const int        newVal);

/**
 * Returns the value of an atomic integer.
 *
 * @param[in] atomicInt  Atomic integer
 * @return               Value of atomic integer
 */
int
atomicInt_get(AtomicInt* const atomicInt);

/**
 * Compares and sets an atomic integer.
 *
 * @param[in,out] atomicInt  Atomic integer to be compared and (possibly) set
 * @param[in]     expectVal  Expected value
 * @param[in]     newVal     New value for atomic integer if current value
 *                           equals `expectVal`
 * @return                   Previous value of atomic integer
 * @asyncsignalsafety        Unsafe
 */
int
atomicInt_compareAndSet(
        AtomicInt* const atomicInt,
        const int        expectVal,
        const int        newVal);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_ATOMIC_INT_H_ */

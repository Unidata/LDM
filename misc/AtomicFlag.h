/**
 * This file defines an flag that can be accessed atomically.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: AtomicFlag.h
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#ifndef MISC_ATOMIC_FLAG_H_
#define MISC_ATOMIC_FLAG_H_

typedef struct atomicFlag AtomicFlag;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new, cleared, atomic flag.
 *
 * @retval     NULL         Failure. `log_add()` called.
 * @return                  New (cleared) atomic flag
 * @asyncsignalsafety       Unsafe
 */
AtomicFlag*
atomicFlag_new(void);

/**
 * Frees an atomic flag.
 *
 * @param[in,out] atomicFlag  Atomic flag to be freed or NULL
 * @asyncsignalsafety         Unsafe
 */
void
atomicFlag_free(AtomicFlag* const atomicFlag);

/**
 * Tests and sets an atomic flag.
 *
 * @param[in,out] atomicFlag  Atomic flag to be tested and (possibly) set
 * @retval        `true`      Flag was already set
 * @retval        `false`     Flag was not already set. Flag is now set.
 * @asyncsignalsafety         Unsafe
 */
bool
atomicFlag_testAndSet(AtomicFlag* const atomicFlag);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_ATOMIC_FLAG_H_ */

/**
 * This file declares a getline(3)-like function.
 *
 * Copyright 2021, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: mygetline.c
 *  Created on: 2021-04-22
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <stddef.h>
#include <stdio.h>

#ifndef MISC_MYGETLINE_H_
#define MISC_MYGETLINE_H_

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Read up to (and including) a newline from a stream and null-terminate it.
 *
 * @param[in,out] lineptr  Input line buffer. `*lineptr` must be `NULL` or a
 *                         pointer returned by `malloc()` or `realloc()` that
 *                         points to `size` bytes of space. Will point to
 *                         allocated space on successful return. Caller should
 *                         free `*lineptr` when it's no longer needed.
 * @param[in,out] size     Number of bytes `*lineptr` points to if not NULL;
 *                         otherwise ignored. Will contain number of bytes
 *                         `*lineptr` points to on successful return.
 * @param[in]     stream   Stream from which to read a line of input
 * @retval        -1       Error. `log_add()` called
 * @retval         0       EOF
 * @return                 Number of bytes read -- excluding the terminating
 *                         NUL. `*lineptr` and `*size` are set.
 */
ssize_t
mygetline(char** const restrict  lineptr,
          size_t* const restrict size,
          FILE* const restrict   stream);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_MYGETLINE_H_ */

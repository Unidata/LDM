/**
 * This file implements a getline(3)-like function.
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

#include "mygetline.h"
#include "log.h"

#include <limits.h>
#include <string.h>

// The getline() function isn't part of _XOPEN_SOURCE=600

ssize_t
mygetline(char** const restrict  lineptr,
          size_t* const restrict size,
          FILE* const restrict   stream)
{
    ssize_t nbytes = -1; // Error

    if (lineptr == NULL || size == NULL) {
        log_add("Invalid argument: lineptr=%p, size=%p", lineptr, size);
    }
    else {
        static const int SIZE = PIPE_BUF;
        char*            line = log_realloc(*lineptr, SIZE, "mygetline() buffer");

        if (line) {
            if (fgets(line, SIZE, stream) == NULL) {
                if (ferror(stream))
                    log_add_syserr("fgets() failure");
                nbytes = -1; // EOF
            }
            else {
                *size = SIZE;
                nbytes = strlen(line);
            } // Line read

			*lineptr = line;
        } // `line` allocated
    } // Valid arguments

    return nbytes;
}

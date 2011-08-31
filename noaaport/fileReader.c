/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#define _XOPEN_SOURCE 500

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "noaaportLog.h"
#include "fifo.h"
#include "fileReader.h" /* Eat own dog food */

/**
 * Returns a new file-reader.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Precondition failure. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
int fileReaderNew(
    const char* const   pathname,   /**< [in] Pathname of file to read or
                                      *  NULL to read standard input stream */
    Fifo* const         fifo,       /**< [in] Pointer to FIFO into which to put
                                      *  data */
    Reader** const      reader)     /**< [out] Pointer to pointer to address of
                                      *  reader */
{
    int status = 0;                 /* default success */
    int fd;                         /* input file descriptor */

    if (NULL == pathname) {
        if ((fd = fileno(stdin)) == -1) {
            NPL_SERROR0(
                    "Couldn't get file-descriptor of standard input stream");
            status = 1;
        }
    }
    else {
        if ((fd = open(pathname, O_RDONLY)) == -1) {
            NPL_SERROR1("Couldn't open file \"%s\"", pathname);
            status = 1;
        }
    }

    if (0 == status) {
        if ((status = readerNew(fd, fifo, sysconf(_SC_PAGESIZE), reader)) 
                != 0) {
            NPL_ADD0("Couldn't create new reader object");
        }
    }

    return status;
}

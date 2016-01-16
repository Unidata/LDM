/*
 *   Copyright 2013, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"
#include "log.h"
#include "fifo.h"
#include "fileReader.h" /* Eat own dog food */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Returns a new file-reader.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Precondition failure. \c log_add() called.
 * @retval 2    O/S failure. \c log_add() called.
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
    bool isStandardInput = NULL == pathname;

    if (isStandardInput) {
        if ((fd = fileno(stdin)) == -1) {
            log_syserr(
                    "Couldn't get file-descriptor of standard input stream");
            status = 1;
        }
    }
    else {
        if ((fd = open(pathname, O_RDONLY)) == -1) {
            log_syserr("Couldn't open file \"%s\"", pathname);
            status = 1;
        }
    }

    if (0 == status) {
        if ((status = readerNew(fd, fifo, sysconf(_SC_PAGESIZE), reader)) 
                != 0) {
            log_add("Couldn't create new reader object");

            if (!isStandardInput)
                (void)close(fd);
        }
    }

    return status;
}

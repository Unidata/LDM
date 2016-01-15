/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: lockAllMemory.c
 * @author: Steven R. Emmerson
 *
 * This file ...
 */


#include "config.h"

#include "mylog.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#if _POSIX_MEMLOCK == -1
    #define UNSUPPORTED 1
#elif _POSIX_MEMLOCK == 0
    #define UNKNOWN 1
#endif

/**
 * Locks a process in physical memory.
 *
 * @retval 0        Success.
 * @retval ENOTSUP  The operation isn't supported. `mylog_add()` called.
 * @retval EAGAIN   Some or all of the memory identified by the operation could
 *                  not be locked when the call was made. `mylog_add()` called.
 * @retval ENOMEM   Locking all of the pages currently mapped into the address
 *                  space of the process would exceed an implementation-defined
 *                  limit on the amount of memory that the process may lock.
 *                  `mylog_add()` called.
 * @retval EPERM    The calling process does not have the appropriate privilege
 *                  to perform the requested operation. `mylog_add()` called.
 */
int lockProcessInMemory(void)
{
    int status;

    #if UNSUPPORTED
        mylog_add("System doesn't support locking a process in memory");
        status = ENOTSUP;
    #else
        #if UNKNOWN
            if (sysconf(_SC_MEMLOCK) <= 0) {
                mylog_add("System doesn't support locking a process in memory");
                status = ENOTSUP; // `_SC_MEMLOCK` can't be invalid
            }
            else {
        #endif
                status = mlockall(MCL_CURRENT|MCL_FUTURE);

                if (status) {
                    mylog_add_syserr("mlockall() failure");
                    status = errno;
                }
        #if UNKNOWN
            }
        #endif
    #endif

    return status;
}

/**
 * Unlocks a process from physical memory.
 */
void unlockProcessFromMemory(void)
{
    #if !UNSUPPORTED
        #if UNKNOWN
            if (sysconf(_SC_MEMLOCK) > 0) {
        #endif
                (void)munlockall();
        #if UNKNOWN
            }
        #endif
    #endif
}

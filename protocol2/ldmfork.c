/*
 *   This file contains the function for fork(2)ing in the LDM context.
 *
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include <config.h>

#include "ldmfork.h"
#include "log.h"
#include "registry.h"

#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

/******************************************************************************
 * Private:
 ******************************************************************************/

/**
 * Returns the maximum number of open file descriptors.
 *
 * @retval -1         Failure. log_add() called.
 * @return            The maximum number of open file descriptors
 */
static int
open_max(void)
{
    static int open_max =
    #ifdef OPEN_MAX
                          OPEN_MAX;
    #else
                          0;
        if (open_max == 0) {
            errno = 0;
            open_max = sysconf(_SC_OPEN_MAX);
            if (open_max < 0) {
                if (errno == 0) {
                    open_max = 256; // Indeterminate. 256 might be inadequate
                }
                else {
                    log_add_syserr("Couldn't get maximum number of file "
                            "descriptors");
                }
            }
        }
    #endif
    return open_max;
}

/**
 * Closes all file descriptors that are equal to or greater than a given file
 * descriptor.
 *
 * @param[in] bottom  The smallest file descriptor to be closed
 * @retval -1         Failure. log_add() called.
 * @return            The maximum number of open file descriptors
 */
static int
close_rest(
        const int bottom)
{
    const int max_open = open_max();
    if (max_open > 0)
        for (int fd = bottom; fd < max_open; fd++)
            (void)close(fd) ;
    return max_open;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Closes all file descriptors that are greater than the ones used for standard
 * I/O and the one used for logging.
 *
 * @retval -1  Failure. log_add() called.
 * @return     The maximum number of open file descriptors.
 */
int close_most_file_descriptors(void)
{
    int fd = log_get_fd();
    if (fd < STDERR_FILENO)
        fd = STDERR_FILENO;
    int status = close_rest(fd + 1);
    return status;
}

/**
 * Forks the current process in the context of the LDM.  Does whatever's
 * necessary before and after the fork to ensure correct behavior.  Terminates
 * the child process if the fork() was successful but an error occurs.
 *
 * @retval -1  Failure. "log_add()" called.
 * @retval  0  Success. The calling process is the child.
 * @return              PID of child process. The calling process is the parent.
 */
pid_t ldmfork(void)
{
    pid_t       pid;

    if (reg_close()) {
        pid = -1;
    }
    else {
        pid = fork();

        if (0 == pid) {
            log_clear(); // So child process starts with no queued messages
            /* Child process */
        }
        else if (-1 == pid) {
            /* System error */
            log_syserr("Couldn't fork a child process");
        }
    }

    return pid;
}

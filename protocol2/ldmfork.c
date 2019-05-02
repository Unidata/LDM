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
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

/******************************************************************************
 * Private:
 ******************************************************************************/

#if 0
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
#endif

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Opens a file descriptor on `/dev/null` if it's closed. If the file
 * descriptor is open, then nothing happens.
 *
 * @param[in] fileno  File descriptor
 * @param[in] flags   `open()` flags
 * @retval  0         Success
 * @retval -1         Failure. log_add() called.
 */
int open_on_dev_null_if_closed(
        const int fileno,
        const int flags)
{
    int status;
    if (fcntl(fileno, F_GETFD) >= 0) {
        status = 0;
    }
    else {
        status = -1;
        int fd = open("/dev/null", flags);
        if (fd < 0) {
            log_add_syserr("Couldn't open /dev/null: flags=%#X", flags);
        }
        else if (fd == fileno) {
            status = 0;
        }
        else {
            if (dup2(fd, fileno) < 0) {
                log_add_syserr("dup2() failure: fd=%d, fileno=%d", fd, fileno);
            }
            else {
                status = 0;
            }
            (void)close(fd);
        }
    }
    return status;
}

/**
 * Ensures that a file descriptor will close if and when a function of the
 * `exec()` family is called.
 *
 * @param[in] fd  File descriptor
 * @retval  0     Success
 * @retval -1     `fd` is not a valid open file descriptor. log_add() called.
 */
int ensure_close_on_exec(
        const int fd)
{
    int status = fcntl(fd, F_GETFD);
    if (status == -1) {
        log_add_syserr("Couldn't get file descriptor flags: fd=%d", fd);
    }
    else if (status & FD_CLOEXEC) {
        status = 0;
    }
    else {
        status = fcntl(fd, F_SETFD, status | FD_CLOEXEC);
        if (status == -1) {
            log_add_syserr("Couldn't set file descriptor to close-on-exec: "
                    "fd=%d", fd);
        }
        else {
            status = 0;
        }
    }
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

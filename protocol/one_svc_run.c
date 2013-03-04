/*
 *   Copyright 2013, University Corporation for Atmospheric Research
 *   All rights reserved.
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

/* 
 * ldm server mainline program module
 */

#include "config.h"

#include <errno.h>
#include <rpc/rpc.h>   /* svc_fdset */
#include <signal.h>    /* sig_atomic_t */
#include <string.h>
#include <sys/time.h>  /* fd_set */

#include "log.h"

#include "ldm.h"        /* one_svc_run() */
#include "autoshift.h"  /* asTimeToShift() */
#include "timestamp.h"
#include "globals.h"
#include "remote.h"


/**
 * Run an RPC server on a single socket (similar to svc_run(3RPC)). Runs until:
 *   1) The socket gets closed; or
 *   2) The timeout expires without any activity; or
 *   3) as_shouldSwitch() returns true; or
 *   4) An error occurs.
 * <p>
 * This function uses the "log" module to accumulate messages.
 *
 * @param xp_sock           The connected socket.
 * @param inactive_timeo    The maximum amount of time to wait with no activity
 *                          on the socket in seconds.
 *
 * @retval 0                Success.  as_shouldSwitch() is true.
 * @retval EBADF            The socket isn't open.
 * @retval EINVAL           Invalid timeout value.
 * @retval ECONNRESET       RPC layer closed socket.  The RPC layer also
 *                          destroyed the associated SVCXPRT structure;
 *                          therefore, that object must not be subsequently
 *                          dereferenced.
 * @retval ETIMEDOUT        "inactive_timeo" time passed without any activity on
 *                          the socket.
 */ 
int
one_svc_run(
    const int                   xp_sock,
    const unsigned              inactive_timeo) 
{
    timestampt                  timeout;
    timestampt                  stimeo;
    fd_set                      fds;
    int                         retCode;

    timeout.tv_sec = inactive_timeo;
    timeout.tv_usec = 0;
    stimeo = timeout;

    FD_ZERO(&fds);
    FD_SET(xp_sock, &fds);

    for (;;) {
        fd_set          readFds = fds;
        int             width = xp_sock + 1;
        timestampt      before;
        int             selectStatus;

        (void)set_timestamp(&before);

        selectStatus = select(width, &readFds, 0, 0, &stimeo);

        (void)exitIfDone(0); /* handles SIGTERM reception */

        if (selectStatus == 0) {
            retCode = ETIMEDOUT;
            break;
        }

        if (selectStatus > 0) {
            /*
             * The socket is ready for reading.
             */
            svc_getreqsock(xp_sock);    /* process socket input */

            (void)exitIfDone(0);

            if (!FD_ISSET(xp_sock, &svc_fdset)) {
                /*
                 * The RPC layer closed the socket and destroyed the associated
                 * SVCXPRT structure.
                 */
                 log_add("one_svc_run(): RPC layer closed connection");
                 retCode = ECONNRESET;
                 break;
            }

            stimeo = timeout;   /* reset select(2) timeout */

            if (as_shouldSwitch()) {    /* always false for upstream LDM-s */
                retCode = 0;
                break;
            }
        }
        else {
            if (errno != EINTR) {
                log_errno();
                log_add("one_svc_run(): select() error on socket %d", xp_sock);
                retCode = errno;
                break;
            }

            {
                timestampt      after;
                timestampt      diff;

                (void)set_timestamp(&after);

                /*
                 * Adjust select(2) timeout.
                 */
                diff = diff_timestamp(&after, &before);
                stimeo = diff_timestamp(&timeout, &diff);
            }
        } 
    }                                   /* indefinite loop */

    return retCode;
}

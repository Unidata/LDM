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
 *   3) as_shouldSwitch() returns true;
 *   4) An error occurs; or
 *   5) The thread is interrupted and the `done` global variable is set.
 * <p>
 * This function uses the "log" module to accumulate messages.
 *
 * @param sock              The connected socket.
 * @param timeout           The maximum amount of time to wait with no activity
 *                          on the socket in seconds.
 *
 * @retval 0                Success.  as_shouldSwitch() is true. Only happens
 *                          for downstream LDM-s.
 * @retval EBADF            The socket isn't open. `log_add()` called.
 * @retval EINVAL           Invalid timeout value. `log_add()` called.
 * @retval ECONNRESET       RPC layer closed socket.  The RPC layer also
 *                          destroyed the associated SVCXPRT structure;
 *                          therefore, that object must not be subsequently
 *                          dereferenced. `log_add()` called.
 * @retval ETIMEDOUT        "timeout" time passed without any activity on
 *                          the socket.
 */ 
int
one_svc_run(
    const int       sock,
    const unsigned  timeout) 
{
    timestampt      canonicalTimeout;
    timestampt      selectTimeout;
    fd_set          fds;

    canonicalTimeout.tv_sec = timeout;
    canonicalTimeout.tv_usec = 0;
    selectTimeout = canonicalTimeout;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    for (;;) {
        fd_set          readFds = fds;
        timestampt      before;
        int             selectStatus;

        (void)set_timestamp(&before);

        selectStatus = select(sock+1, &readFds, 0, 0, &selectTimeout);

        (void)exitIfDone(0); /* handles SIGTERM reception */

        if (selectStatus == 0)
            return ETIMEDOUT;

        if (selectStatus > 0) {
            /*
             * The socket is ready for reading. The following statement calls
             * `ldmprog_5()`, `ldmprog_6()`, or `ldmprog_7()`. Calls
             * SVC_DESTROY() if the connection dies.
             */
            svc_getreqsock(sock);
            (void)exitIfDone(0);

            if (!FD_ISSET(sock, &svc_fdset)) {
                /*
                 * The RPC layer closed the socket and destroyed the associated
                 * SVCXPRT structure.
                 */
                 log_add("RPC layer closed connection");
                 return ECONNRESET;
            }

            if (as_shouldSwitch()) /* always false for upstream LDM-s */
                return 0;

            selectTimeout = canonicalTimeout; /* reset select(2) timeout */
        } /* socket is read-ready */
        else {
            if (errno != EINTR) {
                log_add_syserr("select() error on socket %d", sock);
                return errno;
            }

            {
                timestampt      after;
                timestampt      diff;

                (void)set_timestamp(&after);

                /*
                 * Adjust select(2) timeout.
                 */
                diff = diff_timestamp(&after, &before);
                selectTimeout = diff_timestamp(&canonicalTimeout, &diff);
            }
        } /* select() returned -1 */
    } /* indefinite loop */

    return 0; // Eclipse wants to see a return
}

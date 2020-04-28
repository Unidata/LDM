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
#include <poll.h>
#include <signal.h>    /* sig_atomic_t */
#include <string.h>

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
 * @param timeout           The maximum amount of seconds to wait with no
 *                          activity on the socket. -1 => indefinite wait.
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
    const int sock,
    const int timeout)
{
	const int     timeo = timeout < 0 ? -1 : 1000*timeout; // -1 => indefinite
	struct pollfd pfd = {.fd=sock, .events=POLLRDNORM};

	for (;;) {
		int status = poll(&pfd, 1, timeo);
        (void)exitIfDone(0); /* handles SIGTERM reception */

		if (status < 0) {
			if (errno == EINTR) {
				log_debug("poll() was interrupted");
				/*
				 * Might not be meaningful. For example, the GNUlib
				 * seteuid() function generates a non-standard signal in
				 * order to synchronize UID changes amongst threads.
				 */
				continue;
			}
			log_add_syserr("poll() failure on socket %d", sock);
			return errno;
		}

		if (0 == status) {
			log_debug("Timeout");
			return ETIMEDOUT;
		}

		/*
		 * The socket is ready for reading. The following statement calls
		 * svc_destroy() on error; otherwise, it calls `ldmprog_5()`,
		 * `ldmprog_6()`, or `ldmprog_7()`.
		 */
		svc_getreqsock(sock);
        (void)exitIfDone(0); /* handles SIGTERM reception */

		if (!FD_ISSET(sock, &svc_fdset)) {
			/*
			 * The RPC layer closed the socket and destroyed the associated
			 * SVCXPRT structure.
			 */
			 log_add("RPC layer closed connection on socket %d", sock);
			 return ECONNRESET;
		}

		if (as_shouldSwitch()) /* always false for upstream LDM-s */
			return 0;

		log_debug("RPC message processed");
	} // poll(2) loop

	return 0; // To silence Eclipse
}

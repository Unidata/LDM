/*
 * Library for Up7Down7_test and Down7_test.
 *
 *  Created on: Apr 2, 2020
 *      Author: steve
 */
#include "config.h"

#include "Up7Down7_lib.h"

#include "ldm.h"
#include "log.h"
#include "registry.h"
#include "VirtualCircuit.h"

#include <signal.h>

static sigset_t mostSigMask;

static void setSigHand(void (*sigHandler)(int sig))
{
    static const int interuptSigs[] = { SIGIO, SIGPIPE, SIGINT, SIGTERM,
            SIGHUP, 0};
    static const int restartSigs[] = { SIGUSR1, SIGUSR2, 0 };
    int              status;
    struct sigaction sigact = {}; // Zero initialization

    /*
     * While handling a signal, block all signals except ones that would cause
     * undefined behavior
     */
    sigact.sa_mask = mostSigMask;

    // Handle the following
    sigact.sa_handler = sigHandler;

    // Interrupt system calls for the following
    for (int i = 0; interuptSigs[i]; ++i) {
        status = sigaction(interuptSigs[i], &sigact, NULL);
        // `errno == EINVAL` for `SIGKILL` & `SIGSTOP`, at least
        log_assert(status == 0 || errno == EINVAL);
    }

    // Restart system calls for the following
    sigact.sa_flags = SA_RESTART;
    for (int i = 0; restartSigs[i]; ++i) {
        status = sigaction(restartSigs[i], &sigact, NULL);
        log_assert(status == 0);
    }
}

void ud7_init(void (*sigHandler)(int sig))
{
	localVcEnd = vcEndPoint_new(1, NULL, NULL);

	log_assert(localVcEnd != NULL);

	/*
	 * Create a signal mask that would block all signals except those that
	 * would cause undefined behavior
	 */
	static const int undefSigs[] = { SIGFPE, SIGILL, SIGSEGV, SIGBUS };
	int              status = sigfillset(&mostSigMask);
	log_assert(status == 0);
	for (int i = 0; i < sizeof(undefSigs)/sizeof(undefSigs[0]); ++i) {
		status = sigdelset(&mostSigMask, undefSigs[i]);
		log_assert(status == 0);
	}

	setSigHand(sigHandler);
}

void ud7_free()
{
    vcEndPoint_free(localVcEnd);
    reg_close();
}

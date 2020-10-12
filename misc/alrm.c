/**
 * Alarm related stuff.
 *
 *  Created on: Oct 8, 2020
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "alrm.h"
#include "log.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

static struct sigaction      prevAlarmAction;
static bool                  prevAlarmActionSet = false;
static volatile sig_atomic_t alarmGenerated = false;
static sigset_t              alarmMask;
static struct sigaction      alarmAction = {.sa_flags=0};
static bool                  initialized = false;

static void handler(int sig)
{
	alarmGenerated = true;
}

void alrm_init()
{
	if (!initialized) {
        (void)sigemptyset(&alarmMask);
        (void)sigaddset(&alarmMask, SIGALRM);

        alarmAction.sa_handler = handler;
        (void)sigemptyset(&alarmAction.sa_mask);

        initialized = true;
	}
}

/**
 * Removes any pending SIGALRM.
 */
inline static void rmPending()
{
    sigset_t sigset;
    (void)sigpending(&sigset);
    if (sigismember(&sigset, SIGALRM)) {
    	alarmGenerated = true;
    	int sig;
    	(void)sigwait(&alarmMask, &sig);
    }
}

/**
 * Dismisses any potential or pending SIGALRM.
 */
static void dismiss()
{
	sigset_t prevSigSet;
	(void)sigprocmask(SIG_BLOCK, &alarmMask, &prevSigSet); // Block SIGALRM
	(void)alarm(0) ;    // Eliminate potential SIGALRM generation
	rmPending();        // Remove any pending SIGALRM
	(void)sigprocmask(SIG_SETMASK, &prevSigSet, NULL); // Restore signal mask
}

void alarm_set(const unsigned seconds)
{
	alrm_init();
	(void)sigaction(SIGALRM, &alarmAction, &prevAlarmAction);
	prevAlarmActionSet = true;
	alarmGenerated = false;

	(void)alarm(seconds);
}

void alarm_dismiss()
{
	if (prevAlarmActionSet) {
		dismiss(); // Dismiss any potential or pending SIGALRM

        // Restore previous SIGALRM action
        (void)sigaction(SIGALRM, &prevAlarmAction, NULL);
        prevAlarmActionSet = false;
	}
}

int alarm_generated()
{
    return alarmGenerated;
}

/*
 * Glenn's original, siglongjmp()-based stuff with some modifications and
 * additions -- SRE 2020-10-11
 */

// Must be visible to SET_ALARM() in "alrm.h":
volatile sig_atomic_t alrm_validJmpbuf = 0 ; // `alrm_jumpbuf` is valid?
sigjmp_buf            alrm_jumpbuf;
Sigfunc*              alrm_savHandler = SIG_ERR ;

Sigfunc* alrm_mysignal(int signo, Sigfunc *handler)
{
	struct sigaction sigact, oact ;

	sigact.sa_handler = handler ;
	sigemptyset(&sigact.sa_mask) ;
	sigact.sa_flags = 0 ;

	return (sigaction(signo, &sigact, &oact) == -1)
			? SIG_ERR
			: oact.sa_handler;
}

/*ARGSUSED*/
void alrm_handler(int sig)
{
	if(!alrm_validJmpbuf) {
		/* TODO: something rational */
		return ;
	}
	/* else, safe to do the jump */
	alrm_validJmpbuf = 0 ;
	(void)siglongjmp(alrm_jumpbuf, 1) ;
}

void alrm_clear()
{
	alrm_init(); \
	dismiss(); // Dismiss any potential or pending SIGALRM
	if (alrm_savHandler != SIG_ERR) {
		(void)alrm_mysignal(SIGALRM, alrm_savHandler) ;
		alrm_savHandler = SIG_ERR ;
	}
	alrm_validJmpbuf = 0 ;
}

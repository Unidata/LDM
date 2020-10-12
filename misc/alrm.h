/**
 * Careful alarm interface
 *
 * Copyright 2020, University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */

#ifndef ALRM_H
#define ALRM_H

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

/*
 * Implement a signal function with known, desired characteristics. -Stevens
 */
#if defined(__STDC__)
typedef void Sigfunc(int) ;
#else
typedef void Sigfunc() ;
#endif

/**
 * Sets an alarm.
 *
 * @param[in]  seconds       Amount of time to wait until SIGALRM is generated
 * @retval     0             Success
 */
void alarm_set(const unsigned seconds);

/**
 * Dismisses any potential or pending alarm. Does nothing if `alarm_set()`
 * wasn't previously called.
 */
void alarm_dismiss();

/**
 * Indicates if a SIGALRM was generated after the last call to `alarm_set()`.
 *
 * @retval 1  SIGALRM was generated
 * @retval 0  SIGALRM was not generated
 */
int alarm_generated();

/*
 * Glenn's original stuff with some additions and modifications:
 * -- SRE 2020-10-10
 */

// The following must be visible to SET_ALARM():
extern volatile sig_atomic_t alrm_validJmpbuf; // `alrm_jumpbuf` is valid?
extern sigjmp_buf            alrm_jumpbuf;
extern Sigfunc*              alrm_savHandler;
extern void                  alrm_init(); // Ensures module is initialized

/**
 * Sets a signal handler.
 *
 * @param[in] signo    Signal number
 * @param[in] handler  Signal handler
 * @return             Previous signal handler
 */
Sigfunc* alrm_mysignal(int signo, Sigfunc *handler);

/**
 * SIGALRM handler for SET_ALARM().
 *
 * @param[in] sig  SIGALRM
 */
void alrm_handler(int sig);

/**
 * Sets an alarm.
 *
 * @param[in] seconds    Time until SIGALRM is generated
 * @param[in] jumplabel  `goto` label to jump to upon SIGALRM delivery
 */
#define SET_ALARM(seconds, jumplabel) \
{ \
	alrm_init(); \
	if (sigsetjmp(alrm_jumpbuf, 1)) { \
		/* \
		 * SIGALRM handler called siglongjmp(). Can't happen inside \
		 * alrm_clear(). \
		 */ \
		if (alrm_savHandler != SIG_ERR) { \
			(void)alrm_mysignal(SIGALRM, alrm_savHandler); \
			alrm_savHandler = SIG_ERR; \
		} \
		goto jumplabel; \
	} \
	/* else, first time, we set the jmp env */ \
	alrm_validJmpbuf = 1; \
    \
	alrm_savHandler = alrm_mysignal(SIGALRM, alrm_handler); \
	if (alrm_savHandler == SIG_ERR) { \
		alrm_validJmpbuf = 0; \
	} else { \
		(void)alarm((unsigned)(seconds)); \
	} \
}

/**
 * Dismisses any potential or pending alarm. Safe to call even when SET_ALARM()
 * has not been called
 */
void alrm_clear();
#define CLR_ALARM() alrm_clear()

#endif /* ALRM_H */

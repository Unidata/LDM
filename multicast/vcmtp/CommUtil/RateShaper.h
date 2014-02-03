/*
 * RateShaper.h
 *
 *  Created on: Jan 7, 2012
 *      Author: jie
 */

#ifndef RATESHAPER_H_
#define RATESHAPER_H_

#include "Timer.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


class RateShaper {
public:
	RateShaper();
	virtual ~RateShaper();

	void SetRate(double rate_bps);
	void RetrieveTokens(int num_tokens);

private:
	double rate;	// maximum rate in bytes per second
	int bucket_volume;
	int overflow_tolerance;
	int tokens_in_bucket;
	int token_unit;
	int token_time_interval;	// in microseconds

	CpuCycleCounter cpu_counter;
	double last_check_time;
	struct timespec time_spec;

	timer_t timer_id;
	struct sigevent signal_event;
	struct sigaction signal_action;
	struct itimerspec timer_specs;

	void StartTimer();
	static void AddTokensHandler(int cause, siginfo_t *si, void *ucontext);
	void AddTokens();

};

#endif /* RATESHAPER_H_ */

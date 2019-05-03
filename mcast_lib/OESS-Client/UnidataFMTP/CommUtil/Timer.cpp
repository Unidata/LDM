/*
 * Timer.cpp
 *
 *  Created on: Jan 7, 2012
 *      Author: jie
 */

#include "Timer.h"

double Timer::CPU_MHZ = GetCPUMhz();
CpuCycleCounter Timer::start_time_counter;

Timer::Timer() {
	// TODO Auto-generated constructor stub

}

Timer::~Timer() {
	// TODO Auto-generated destructor stub
}



// Global functions
void AccessCPUCounter(unsigned *hi, unsigned *lo) {
	asm("rdtsc; movl %%edx, %0; movl %%eax, %1"
			: "=r" (*hi), "=r" (*lo)
			:
			: "%edx", "%eax");
}


double GetElapsedCycles(unsigned cycle_hi, unsigned cycle_lo) {
	unsigned ncycle_hi, ncycle_lo;
	unsigned hi, lo, borrow;
	double result;

	AccessCPUCounter(&ncycle_hi, &ncycle_lo);

	lo = ncycle_lo - cycle_lo;
	borrow = lo > ncycle_lo;
	hi = ncycle_hi - cycle_hi - borrow;
	result = (double) hi * (1 << 30) * 4 + lo;
	if (result < 0) {
		cout << "GetElapsedCycles(): counter returns negative value" << endl;
	}
	return result;
}


double GetCPUMhz() {
	double rate;
	unsigned hi, lo;
	AccessCPUCounter(&hi, &lo);
	sleep(1);
	rate = GetElapsedCycles(hi, lo) / 1 / 1000000;
	return rate;
}

double GetCurrentTime() {
	return GetElapsedCycles(Timer::start_time_counter.hi, Timer::start_time_counter.lo) / 1000000.0 / Timer::CPU_MHZ;
}

double GetElapsedSeconds(CpuCycleCounter lastCount) {
	return GetElapsedCycles(lastCount.hi, lastCount.lo) / 1000000.0 / Timer::CPU_MHZ;
}

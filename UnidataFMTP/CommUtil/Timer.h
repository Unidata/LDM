/*
 * Timer.h
 *
 *  Created on: Jan 7, 2012
 *      Author: jie
 */

#ifndef TIMER_H_
#define TIMER_H_

#include <iostream>
#include <stdlib.h>
#include <unistd.h>


using namespace std;

struct CpuCycleCounter {
	unsigned hi;
	unsigned lo;
};


void AccessCPUCounter(unsigned *hi, unsigned *lo);
double GetElapsedCycles(unsigned hi, unsigned lo);
double GetElapsedSeconds(CpuCycleCounter lastCount);
double GetCPUMhz();
double GetCurrentTime();





// Timer Class: high precision timer using the cpu cycle counter
class Timer {
public:
	Timer();
	virtual ~Timer();

	static struct CpuCycleCounter start_time_counter;
	static double CPU_MHZ;
};

#endif /* TIMER_H_ */

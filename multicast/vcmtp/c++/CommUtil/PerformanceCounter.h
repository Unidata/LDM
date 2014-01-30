/*
 * CpuUsageCounter.h
 *
 *  Created on: Jan 7, 2012
 *      Author: jie
 */

#ifndef CPUUSAGECOUNTER_H_
#define CPUUSAGECOUNTER_H_

#include "Timer.h"
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>

using namespace std;


class PerformanceCounter {
public:
	PerformanceCounter();
	PerformanceCounter(int interval);
	~PerformanceCounter();

	void Start();
	void Stop();
	void SetInterval(int milliseconds);

	void SetCPUFlag(bool flag);
	void SetUDPRecvBuffFlag(bool flag);

	string	GetCPUMeasurements();
	int		GetAverageCpuUsage();


private:
	int 	interval;   // measurement interval in milliseconds
	bool 	keep_running;

	bool    measure_cpu;
	bool    measure_udp_recv_buffer;
	bool	thread_exited;

	vector<int>		cpu_values;
	vector<int>		udp_buffer_values;

	void	MeasureCPUInfo(ofstream& output);
	void 	MeasureUDPRecvBufferInfo(ofstream& output);
	int		HexToDecimal(char* start, char* end);

	pthread_t count_thread;
	static void* StartCountThread(void* ptr);
	void RunCountThread();
};

#endif /* CPUUSAGECOUNTER_H_ */

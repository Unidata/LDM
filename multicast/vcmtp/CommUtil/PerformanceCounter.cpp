/*
 * CpuUsageCounter.cpp
 *
 *  Created on: Jan 7, 2012
 *      Author: jie
 */

#include "PerformanceCounter.h"

PerformanceCounter::PerformanceCounter() : interval(1000) {
	keep_running = false;

	measure_cpu = true;
	measure_udp_recv_buffer = false;
}


PerformanceCounter::PerformanceCounter(int interval) : interval(interval) {
	keep_running = false;

	measure_cpu = false;
	measure_udp_recv_buffer = false;
}

PerformanceCounter::~PerformanceCounter() {
	// TODO Auto-generated destructor stub
}


void PerformanceCounter::SetInterval(int milliseconds) {
	interval = milliseconds;
}


void PerformanceCounter::SetCPUFlag(bool flag) {
	measure_cpu = flag;
}


void PerformanceCounter::SetUDPRecvBuffFlag(bool flag) {
	measure_udp_recv_buffer = flag;
}



void PerformanceCounter::Start() {
	keep_running = true;
	cpu_values.clear();
	udp_buffer_values.clear();
	pthread_create(&count_thread, NULL, &PerformanceCounter::StartCountThread, this);
}


void PerformanceCounter::Stop() {
	if (keep_running) {
		keep_running = false;
		thread_exited = false;
		while (!thread_exited)
			usleep(10000);
	}
}


void* PerformanceCounter::StartCountThread(void* ptr) {
	((PerformanceCounter*)ptr)->RunCountThread();
	return NULL;
}


void PerformanceCounter::RunCountThread() {
	ofstream output("resource_usage.csv", ofstream::out | ofstream::trunc);
	output << "Measure Time (sec),";
	if (measure_cpu) {
		output << "System Time (sec),User Time (sec),CPU Usage (%),";
	}
	if (measure_udp_recv_buffer) {
		output << "Buffer Occupancy (Bytes)";
	}
	output << endl;

	CpuCycleCounter cpu_counter;
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);

	struct rusage old_usage, new_usage;
	getrusage(RUSAGE_SELF, &old_usage);

	double interval_sec = interval / 1000.0;
	double measure_time = 0.0;
	double elapsed_sec;
	double cpu_time, user_time, sys_time;
	double usage_ratio;
	int usage_percent;

	while (keep_running) {
		while ((elapsed_sec = GetElapsedSeconds(cpu_counter)) < interval_sec) {
			usleep( (interval_sec - elapsed_sec) * 1000000);
		}
		measure_time += elapsed_sec;
		output << measure_time << ",";

		if (measure_cpu) {
			getrusage(RUSAGE_SELF, &new_usage);
			user_time = (new_usage.ru_utime.tv_sec - old_usage.ru_utime.tv_sec)
						+ (new_usage.ru_utime.tv_usec - old_usage.ru_utime.tv_usec) * 0.000001;
			sys_time = (new_usage.ru_stime.tv_sec - old_usage.ru_stime.tv_sec)
						+ (new_usage.ru_stime.tv_usec - old_usage.ru_stime.tv_usec) * 0.000001;
			cpu_time = user_time + sys_time;

			usage_ratio = cpu_time / elapsed_sec;
			usage_percent = usage_ratio * 100;
			//if (usage_percent > 100)
			//	usage_percent = 100;

			cpu_values.push_back(usage_percent);

			output << sys_time << "," << user_time << "," << usage_percent << ",";

			old_usage = new_usage;
		}


		if (measure_udp_recv_buffer) {
			MeasureUDPRecvBufferInfo(output);
		}

		output << endl;
		AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	}

	//cout << "Performance Counter has been stopped." << endl;
	output.close();
	thread_exited = true;
}


string	PerformanceCounter::GetCPUMeasurements() {
	string res = "";
	char buf[10];
	for (int i = 0; i < cpu_values.size(); i++) {
		sprintf(buf, "%d", cpu_values[i]);
		res = res + buf + " ";
	}
	return res;
}


int	PerformanceCounter::GetAverageCpuUsage() {
	if (cpu_values.size() <= 2)
		return 0;

	int sum = 0;
	for (int i = 1; i < cpu_values.size() - 1; i++) {
		sum += cpu_values[i];
	}

	return (sum / (cpu_values.size() - 2));
}


void PerformanceCounter::MeasureCPUInfo(ofstream& output) {

}


void PerformanceCounter::MeasureUDPRecvBufferInfo(ofstream& output) {
	FILE * fp = popen("cat /proc/net/udp", "r");
	if (fp) {
		char *p = NULL, *e;
		size_t n;
		while ((getline(&p, &n, fp) > 0) && p) {
			if ( (p = strstr(p, ":2AF9")) ) {
				p += 32;
				if ( (e = strchr(p, ' ')) ) {
					*e = '\0';
					int buffer_size = HexToDecimal(p, e);
					output << p << "," << buffer_size;
					pclose(fp);
					return;
				}
			}
		}
	}
	pclose(fp);
}


int	PerformanceCounter::HexToDecimal(char* start, char* end) {
	int size = 0;
	char* pos = start;
	while (pos != end) {
		if (*pos > '0' && *pos < '9') {
			size = size * 16 + (*pos - '0');
		}
		else if (*pos >= 'a' && *pos <= 'f' ) {
			size = size * 16 + (*pos - 'a' + 10);
		}
		else if (*pos >= 'A' && *pos <= 'F') {
			size = size * 16 + (*pos - 'A' + 10);
		}
		else
			size *= 16;

		pos++;
	}
	return size;
}











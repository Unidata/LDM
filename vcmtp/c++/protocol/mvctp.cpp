/*
 * mvctp.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: jie
 */
#include "mvctp.h"

FILE*  MVCTP::log_file = NULL;
bool MVCTP::is_log_enabled;

// Must be called before starting MVCTP activities
void MVCTPInit() {
	AccessCPUCounter(&Timer::start_time_counter.hi, &Timer::start_time_counter.lo);
	MVCTP::log_file = NULL;
	MVCTP::is_log_enabled = false;
}

void Log(char* format, ...) {
	if (!MVCTP::is_log_enabled)
		return;

	if (MVCTP::log_file == NULL) {
		MVCTP::log_file = fopen("mvctp_run.log", "w");
	}

	va_list args;
	va_start (args, format);
	vfprintf (MVCTP::log_file, format, args);
	//fflush(log_file);
	va_end (args);
}

void CreateNewLogFile(const char* file_name) {
	if (MVCTP::log_file != NULL) {
		fclose(MVCTP::log_file);
	}

	MVCTP::log_file = fopen(file_name, "w");
}


void SysError(string s) {
	perror(s.c_str());
	exit(-1);
}


bool operator==(const MvctpNackMessage& l, const MvctpNackMessage& r) {
	return (l.seq_num == r.seq_num && l.data_len == r.data_len);
}

bool operator<(const MvctpNackMessage& l, const MvctpNackMessage& r) {
	return ( (l.seq_num < r.seq_num) ||
			 (l.seq_num == r.seq_num && l.data_len < r.data_len));
}





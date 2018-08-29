/*
 * fmtp.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: jie
 */
#include "fmtp.h"

FILE*  FMTP::log_file = NULL;
bool FMTP::is_log_enabled;

// Must be called before starting FMTP activities
void FMTPInit() {
	AccessCPUCounter(&Timer::start_time_counter.hi, &Timer::start_time_counter.lo);
	FMTP::log_file = NULL;
	FMTP::is_log_enabled = false;
}

void Log(char* format, ...) {
	if (!FMTP::is_log_enabled)
		return;

	if (FMTP::log_file == NULL) {
		FMTP::log_file = fopen("fmtp_run.log", "w");
	}

	va_list args;
	va_start (args, format);
	vfprintf (FMTP::log_file, format, args);
	//fflush(log_file);
	va_end (args);
}

void CreateNewLogFile(const char* file_name) {
	if (FMTP::log_file != NULL) {
		fclose(FMTP::log_file);
	}

	FMTP::log_file = fopen(file_name, "w");
}


void SysError(string s) {
	perror(s.c_str());
	// exit(-1);
}


bool operator==(const FmtpNackMessage& l, const FmtpNackMessage& r) {
	return (l.seq_num == r.seq_num && l.data_len == r.data_len);
}

bool operator<(const FmtpNackMessage& l, const FmtpNackMessage& r) {
	return ( (l.seq_num < r.seq_num) ||
			 (l.seq_num == r.seq_num && l.data_len < r.data_len));
}





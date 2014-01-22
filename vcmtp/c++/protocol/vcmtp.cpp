/*
 * vcmtp.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: jie
 */
#include "vcmtp.h"

FILE*  VCMTP::log_file = NULL;
bool VCMTP::is_log_enabled;

// Must be called before starting VCMTP activities
void VCMTPInit() {
	AccessCPUCounter(&Timer::start_time_counter.hi, &Timer::start_time_counter.lo);
	VCMTP::log_file = NULL;
	VCMTP::is_log_enabled = false;
}

void Log(char* format, ...) {
	if (!VCMTP::is_log_enabled)
		return;

	if (VCMTP::log_file == NULL) {
		VCMTP::log_file = fopen("vcmtp_run.log", "w");
	}

	va_list args;
	va_start (args, format);
	vfprintf (VCMTP::log_file, format, args);
	//fflush(log_file);
	va_end (args);
}

void CreateNewLogFile(const char* file_name) {
	if (VCMTP::log_file != NULL) {
		fclose(VCMTP::log_file);
	}

	VCMTP::log_file = fopen(file_name, "w");
}


void SysError(string s) {
	perror(s.c_str());
	exit(-1);
}


bool operator==(const VcmtpNackMessage& l, const VcmtpNackMessage& r) {
	return (l.seq_num == r.seq_num && l.data_len == r.data_len);
}

bool operator<(const VcmtpNackMessage& l, const VcmtpNackMessage& r) {
	return ( (l.seq_num < r.seq_num) ||
			 (l.seq_num == r.seq_num && l.data_len < r.data_len));
}





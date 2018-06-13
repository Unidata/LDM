/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: log_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the `log` module.
 */

#include "config.h"

#include "log.h"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024
#endif

static char* progname;
static const char tmpPathname[] = "/tmp/log_test.log";
static const char tmpPathname1[] = "/tmp/log_test.log.1";

/**
 * Only called once.
 */
static int setup(void)
{
    return 0;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static int numLines(
        const char* const pathname)
{
    char cmd[_POSIX_MAX_INPUT];
    (void)snprintf(cmd, sizeof(cmd), "wc -l %s", pathname);
    FILE* pipe = popen(cmd, "r");
    int n = -1;
    (void)fscanf(pipe, "%d", &n);
    (void)fclose(pipe);
    return n;
}

static void logMessages(void)
{
    log_error_q("Error");
    log_warning_q("Warning");
    log_notice_q("Notice");
    log_info_q("Information");
    log_debug_1("Debug");
}

static void vlogMessage(
        const log_level_t level,
        const char* const   format,
        ...)
{
    va_list args;
    va_start(args, format);
    log_vlog_q(level, format, args);
    va_end(args);
}

static void vlogMessages(void)
{
    vlogMessage(LOG_LEVEL_ERROR, "%s", "Error message");
    vlogMessage(LOG_LEVEL_WARNING, "%s", "Warning");
    vlogMessage(LOG_LEVEL_NOTICE, "%s", "Notice");
    vlogMessage(LOG_LEVEL_INFO, "%s", "Informational message");
    vlogMessage(LOG_LEVEL_DEBUG, "%s", "Debug message");
}

static void make_expected_id(
        char* const restrict       id,
        const size_t               size,
        const char* const restrict name,
        const bool                 is_feeder)
{
    int status;
#if WANT_LOG4C
    status = snprintf(id, size, "%s.%s.%s", progname,
                    is_feeder ? "feeder" : "notifier", name);
#else
    char tmp_name[_POSIX_HOST_NAME_MAX+1];
    (void)strncpy(tmp_name, name, sizeof(tmp_name));
    for (char* cp = strchr(tmp_name, '.'); cp != NULL; cp = strchr(cp, '.'))
        *cp = '_';
    status = snprintf(id, size, "%s(%s)", name, is_feeder ? "feed" : "noti");
#endif
    CU_ASSERT_TRUE(status > 0);
    CU_ASSERT_TRUE(status < size);
}

static void test_init_fini(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    log_fini();
}

static void test_log_open_file(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();

    log_fini();

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_log_open_stderr(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = log_set_destination("-");
    CU_ASSERT_EQUAL(status, 0);
    const char* actual = log_get_destination();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, "-");

    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();

    log_fini();
}

static void test_log_open_default(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    const char* actual = log_get_destination();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, "-"); // default is standard error stream
    log_error_q("Standard error stream");

    status = log_set_destination(tmpPathname);
    actual = log_get_destination();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, tmpPathname);
    log_error_q("File \"%s\"", tmpPathname);

    log_fini();
}

static void test_log_levels(void)
{
    int status;
    log_level_t logLevels[] = {LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
            LOG_LEVEL_NOTICE, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};
    for (int nlines = 1; nlines <= sizeof(logLevels)/sizeof(*logLevels);
            nlines++) {
        status = log_init(progname);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        (void)unlink(tmpPathname);
        status = log_set_destination(tmpPathname);
        CU_ASSERT_EQUAL(status, 0);

        log_set_level(logLevels[nlines-1]);
        logMessages();

        log_fini();

        int n = numLines(tmpPathname);
        CU_ASSERT_EQUAL(n, nlines);

        //if (n != nlines)
            //exit(1);
    }
    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_lower_level_not_clear(void)
{
    int status;
    log_level_t logLevels[] = {LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
            LOG_LEVEL_NOTICE, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};
    for (int index = 0; index < sizeof(logLevels)/sizeof(*logLevels); index++) {
        status = log_init(progname);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        (void)unlink(tmpPathname);
        status = log_set_destination(tmpPathname);
        CU_ASSERT_EQUAL(status, 0);

        int level = logLevels[index];
        log_set_level(level);
        log_add("Logging level %d", level);

        level--;
        log_log_q(level, "Logging level %d", level);

        log_flush(++level);

        log_fini();

        int n = numLines(tmpPathname);
        CU_ASSERT_EQUAL(n, 1);

        //if (n != nlines)
            //exit(1);
    }
    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_log_get_level(void)
{
    log_level_t logLevels[] = {LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
            LOG_LEVEL_NOTICE, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    log_level_t level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_NOTICE);

    for (int i = 0; i < sizeof(logLevels)/sizeof(*logLevels); i++) {
        log_level_t expected = logLevels[i];
        (void)log_set_level(expected);
        log_level_t actual = log_get_level();
        CU_ASSERT_EQUAL(actual, expected);
    }

    log_fini();
}

static void test_log_modify_id(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    char expected[256];
    make_expected_id(expected, sizeof(expected), "foo", true);
    (void)log_set_upstream_id("foo", true);
    const char* actual = log_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

    make_expected_id(expected, sizeof(expected), "bar", false);
    (void)log_set_upstream_id("bar", false);
    actual = log_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

#if WANT_LOG4C
    make_expected_id(expected, sizeof(expected), "128_117_140_56", false);
#else
    make_expected_id(expected, sizeof(expected), "128.117.140.56", false);
#endif
    (void)log_set_upstream_id("128.117.140.56", false);
    actual = log_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

    log_fini();
}

static void test_log_roll_level(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    log_set_level(LOG_LEVEL_ERROR);
    log_level_t level;

    log_roll_level();
    level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_WARNING);

    log_roll_level();
    level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_NOTICE);

    log_roll_level();
    level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_INFO);

    log_roll_level();
    level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_DEBUG);

    log_roll_level();
    level = log_get_level();
    CU_ASSERT_EQUAL(level, LOG_LEVEL_ERROR);

    log_fini();
}

static void test_log_vlog(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    vlogMessages();

    log_fini();

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_log_set_output(void)
{
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    static const char* outputs[] = {"", "-", tmpPathname};
    for (int i = 0; i < sizeof(outputs)/sizeof(outputs[0]); i++) {
        const char* expected = outputs[i];
        status = log_set_destination(expected);
        CU_ASSERT_EQUAL(status, 0);
        const char* actual = log_get_destination();
        CU_ASSERT_PTR_NOT_NULL(actual);
        CU_ASSERT_STRING_EQUAL(actual, expected);
    }

    log_fini();
}

static void test_log_add(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    log_add("LOG_ADD message 1");
    log_add("LOG_ADD message 2");
    log_error_q("LOG_ERROR message");

    log_fini();

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 3);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_log_syserr(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    log_errno_q(ENOMEM, NULL);
    log_errno_q(ENOMEM, "LOG_ERRNO() previous message is part of this one");
    log_errno_q(ENOMEM, "LOG_ERRNO() previous message is part of this one "
            "#%d", 2);
    errno = EEXIST;
    log_syserr_q(NULL);
    log_syserr_q("log_syserr_q() previous message is part of this one");
    log_syserr_q("log_syserr_q() previous message is part of this one #%d", 2);

    log_fini();

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 10);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_log_refresh(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();
    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = rename(tmpPathname, tmpPathname1);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    log_refresh();

    logMessages();
    log_fini();
    n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = unlink(tmpPathname1);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_sighup_log(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();
    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = rename(tmpPathname, tmpPathname1);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    (void)raise(SIGUSR1);

    logMessages();
    n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    log_fini();

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = unlink(tmpPathname1);
    CU_ASSERT_EQUAL(status, 0);
}

static void signal_handler(
        const int sig)
{
    if (sig == SIGUSR1)
        log_refresh();
}

static void test_sighup_prog(void)
{
    (void)unlink(tmpPathname);
    int status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    struct sigaction sigact, oldsigact;
    (void) sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    sigact.sa_handler = signal_handler;
    status = sigaction(SIGUSR1, &sigact, &oldsigact);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    logMessages();
    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = rename(tmpPathname, tmpPathname1);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    (void)raise(SIGUSR1);

    logMessages();
    n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = sigaction(SIGUSR1, &oldsigact, NULL);
    CU_ASSERT_EQUAL(status, 0);

    log_fini();

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = unlink(tmpPathname1);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_change_file(void)
{
    (void)unlink(tmpPathname);
    (void)unlink(tmpPathname1);

    int status;
    status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();

    int n;
    n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = log_set_destination(tmpPathname1);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();

    n = numLines(tmpPathname1);
    CU_ASSERT_EQUAL(n, 5);

    log_fini();

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = unlink(tmpPathname1);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_fork(void)
{
    (void)unlink(tmpPathname);

    int status;
    status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = log_set_destination(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
    status = log_set_level(LOG_LEVEL_DEBUG);
    CU_ASSERT_EQUAL(status, 0);
    logMessages();

    pid_t pid = fork();
    CU_ASSERT_TRUE_FATAL(pid != -1);
    if (pid == 0) {
        // Child
        logMessages();
        exit(0);
    }
    else {
        // Parent
        int child_status;
        status = wait(&child_status);
        CU_ASSERT_EQUAL_FATAL(status, pid);
        CU_ASSERT_TRUE_FATAL(WIFEXITED(child_status));
        CU_ASSERT_EQUAL_FATAL(WEXITSTATUS(child_status), 0);
    }

    int n;
    n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 10);

    log_fini();
    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

/**
 * Returns the time interval between two times.
 *
 * @param[in] later    The later time
 * @param[in] earlier  The earlier time.
 * @return             The time interval, in seconds, between the two times.
 */
static double duration(
    const struct timeval*   later,
    const struct timeval*   earlier)
{
    return (later->tv_sec - earlier->tv_sec) +
        1e-6*(later->tv_usec - earlier->tv_usec);
}

static void test_performance(void)
{
    int status;
    status = log_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = log_set_destination("/dev/null");
    CU_ASSERT_EQUAL(status, 0);

    struct timeval start;
    (void)gettimeofday(&start, NULL);

    const long num_messages = 100000;
    for (long i = 0; i < num_messages; i++)
        log_error_q("Error message %ld", i);

    struct timeval stop;
    (void)gettimeofday(&stop, NULL);
    double dur = duration(&stop, &start);

    status = log_set_destination("-");
    CU_ASSERT_EQUAL(status, 0);
    log_notice_q("%ld printed messages in %g seconds = %g/s", num_messages, dur,
            num_messages/dur);

    status = log_set_destination("/dev/null");
    CU_ASSERT_EQUAL(status, 0);

    (void)gettimeofday(&start, NULL);

    for (long i = 0; i < num_messages; i++)
        log_debug_1("Debug message %ld", i);

    (void)gettimeofday(&stop, NULL);
    dur = duration(&stop, &start);

    status = log_set_destination("-");
    CU_ASSERT_EQUAL(status, 0);
    log_notice_q("%ld unprinted messages in %g seconds = %g/s", num_messages, dur,
            num_messages/dur);

    log_fini();
}

int main(
        const int    argc,
        char* const* argv)
{
    int exitCode = 1;
    progname = basename(argv[0]);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (       CU_ADD_TEST(testSuite, test_init_fini)
                    && CU_ADD_TEST(testSuite, test_init_fini)
                    && CU_ADD_TEST(testSuite, test_log_get_level)
                    && CU_ADD_TEST(testSuite, test_log_roll_level)
                    && CU_ADD_TEST(testSuite, test_log_modify_id)
                    && CU_ADD_TEST(testSuite, test_log_set_output)
                    && CU_ADD_TEST(testSuite, test_log_open_stderr)
                    && CU_ADD_TEST(testSuite, test_log_open_file)
                    && CU_ADD_TEST(testSuite, test_log_open_default)
                    && CU_ADD_TEST(testSuite, test_log_levels)
                    //&& CU_ADD_TEST(testSuite, test_lower_level_not_clear)
                    && CU_ADD_TEST(testSuite, test_log_vlog)
                    && CU_ADD_TEST(testSuite, test_log_add)
                    && CU_ADD_TEST(testSuite, test_log_syserr)
                    && CU_ADD_TEST(testSuite, test_log_refresh)
                    && CU_ADD_TEST(testSuite, test_sighup_log)
                    && CU_ADD_TEST(testSuite, test_sighup_prog)
                    && CU_ADD_TEST(testSuite, test_change_file)
                    && CU_ADD_TEST(testSuite, test_fork)
                    && CU_ADD_TEST(testSuite, test_performance)
                    /*
                    */) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}

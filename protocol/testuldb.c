/**
 * Copyright 2013 University Corporation for Atmospheric Research. All Rights
 * reserved. See file COPYRIGHT in the top-level source-directory for copying
 * and redistribution conditions.
 */
#include "config.h"

#ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE 500
#endif

#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include "uldb.h"
#include "log.h"
#include "prod_class.h"

static const prod_spec spec_some = { ANY, "A", 0 };
static const prod_class_t clss_some = { { 0, 0 }, /* TS_ZERO */
{ 0x7fffffff, 999999 }, /* TS_ENDT */
{ 1, (prod_spec *) &spec_some /* cast away const */
} };

static time_t   stop_time;
static double   sleep_amount;



static unsigned get_size(void)
{
    unsigned    count;
    const int   status = uldb_getSize(&count);

    if (status) {
        LOG_ADD0("Couldn't get size of database");
        log_log(LOG_ERR);
        CU_ASSERT_EQUAL(status, 0);
    }

    return count;
}

static void clear(void)
{
    do {
        int         status;
        uldb_Iter*  iter;

        if (status = uldb_getIterator(&iter)) {
            LOG_ADD0("Couldn't get iterator");
            log_log(LOG_ERR);
            CU_ASSERT_TRUE(0);
        }
        else {
            const uldb_Entry*   entry;
            int                 terminated = 0;

            for (entry = uldb_iter_firstEntry(iter); NULL != entry; entry =
                    uldb_iter_nextEntry(iter)) {
                const pid_t pid = uldb_entry_getPid(entry);
                if (status = kill(pid, SIGTERM)) {
                    LOG_ADD1("Couldn't terminate process %d", pid);
                    CU_ASSERT_TRUE(0);
                }
                else {
                    terminated = 1;
                }
            }

            uldb_iter_free(iter);

            if (terminated)
                sleep(1); /* allow child processes to adjust ULDB */
        }
    } while (get_size() > 0);
}

/**
 * Only called once.
 */
static int setup(
        void)
{
    /*
     * Calls to CU_ASSERT*() are not allowed.
     */
    int status = uldb_delete(__FILE__);

    if (status) {
        if (status == ULDB_EXIST) {
            log_clear();
        }
        else {
            log_add("Couldn't delete existing database");
            log_log(LOG_ERR);
            return 1;
        }
    }

    if (uldb_create(__FILE__, 0)) {
        log_add("Couldn't create database");
        log_log(LOG_ERR);
        return 1;
    }

    return 0;
}

/**
 * Only called once.
 */
static int teardown(
        void)
{
    /*
     * Calls to CU_ASSERT*() are not allowed.
     */
    clear();

    sleep(1); /** allow child processes to remove themselves from the ULDB */

    if (uldb_close()) {
        log_add("Couldn't close database");
        log_log(LOG_ERR);
        return 1;
    }

    if (uldb_delete(__FILE__)) {
        log_add("Couldn't delete database");
        log_log(LOG_ERR);
        return 1;
    }

    return 0;
}

static void test_nil(
        void)
{
    int status;
    struct sockaddr_in sockAddr;
    unsigned count;
    prod_class_t* allowed;

    (void) memset(&sockAddr, 0, sizeof(sockAddr));

    CU_ASSERT_EQUAL(get_size(), 0);

    status = uldb_addProcess(-1, 6, &sockAddr, &_clss_all, &allowed, 0, 1);
    CU_ASSERT_EQUAL(status, ULDB_ARG);
    log_clear();
}

static struct sockaddr_in new_sock_addr(void)
{
    static struct sockaddr_in   sockAddr;

    sockAddr.sin_addr.s_addr++;

    return sockAddr;
}

static void handle_sigterm(
        const int   sig)
{
    return;
}

static pid_t spawn_upstream(
        const int                       proto,
        const struct sockaddr_in* const sockAddr,
        const prod_class_t* const       desired,
        const int                       isNotifier,
        const int                       isPrimary,
        void                    (*const action)(void))
{
    pid_t   pid = fork();

    if (pid < 0) {
        LOG_SERROR0("Couldn't fork child process");
        return pid;
    }

    if (pid > 0) {
        /* Parent */
        return pid;
    }

    {
        /* Child */
        int                 status;
        prod_class_t*       allowed;
        struct sigaction    act;

        act.sa_handler = handle_sigterm;
        (void)sigemptyset(&act.sa_mask);
        act.sa_flags = 0;

        CU_ASSERT_EQUAL(sigaction(SIGTERM, &act, NULL), 0);

        pid = getpid();
        status = uldb_addProcess(pid, proto, sockAddr, desired, &allowed,
                isNotifier, isPrimary);
        if (status) {
            LOG_ADD1("Couldn't add upstream LDM process %d", pid);
            log_log(LOG_ERR);
            status = 1;
        }
        else {
            if (!clss_eq(&_clss_all, allowed)) {
                LOG_ADD0("Allowed != desired");
                log_log(LOG_ERR);
                status = 2;
            }
            else {
                action();
                status = 0;
            }

            free_prod_class(allowed);

            if (uldb_remove(pid)) {
                LOG_ADD1("Couldn't remove upstream LDM process %d", pid);
                log_log(LOG_ERR);
                status = 3;
            }

            if (uldb_close()) {
                LOG_ADD0("Couldn't close ULDB database");
                log_log(LOG_ERR);
                status = 4;
            }
        }

        exit(status);
    }
}

static void pause_action(void)
{
    (void)pause();
}

static pid_t spawn_reg_upstream(
        const int                       proto,
        const struct sockaddr_in* const sockAddr,
        const prod_class_t* const       desired,
        const int                       isNotifier,
        const int                       isPrimary)
{
    return spawn_upstream(proto, sockAddr, desired, isNotifier, isPrimary,
            pause_action);
}

static void perf_action(void)
{
    struct timespec request;
    double          seconds;

#define ONE_BILLION 1000000000

    request.tv_nsec = (long)modf(sleep_amount, &seconds) * ONE_BILLION;
    request.tv_sec = (int)seconds;

    if (request.tv_nsec > ONE_BILLION) {
        request.tv_nsec -= ONE_BILLION;
        request.tv_sec++;
    }

    (void)nanosleep(&request, NULL);
}

static pid_t spawn_perf_upstream(void)
{
    time_t                          now = time(NULL);
    pid_t                           pid;
    static struct sockaddr_in       sockAddr;
    static const struct sockaddr_in constSockAddr;
    const double                    time_remaining = stop_time - now;

    sleep_amount = time_remaining < 0
            ? 0
            : drand48() * time_remaining;

    sockAddr = (drand48() < 0.5)
            ? constSockAddr
            : new_sock_addr();

    pid = spawn_upstream(6, &sockAddr, &_clss_all, drand48() < 0.5, 1,
            perf_action);

    return pid;
}

static pid_t set_uldb(
        const int                       proto,
        const struct sockaddr_in* const sockAddr,
        const prod_class_t* const       desired,
        const int                       isNotifier,
        const int                       isPrimary)
{
    int                 status;
    pid_t               pid;
    prod_class_t*       allowed;
    unsigned            count;
    uldb_Iter*          iter;
    const uldb_Entry*   entry;

    clear();

    pid = spawn_reg_upstream(proto, sockAddr, desired, isNotifier, isPrimary);
    CU_ASSERT_TRUE(pid > 0);

    sleep(1);
    CU_ASSERT_EQUAL(get_size(), 1);

    status = uldb_getIterator(&iter);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL(iter);

    for (entry = uldb_iter_firstEntry(iter); NULL != entry; entry =
            uldb_iter_nextEntry(iter)) {
        CU_ASSERT_EQUAL(uldb_entry_getPid(entry), pid);
        CU_ASSERT_EQUAL(uldb_entry_getProtocolVersion(entry), proto);
        CU_ASSERT_EQUAL(uldb_entry_isNotifier(entry), isNotifier);
        CU_ASSERT_EQUAL(uldb_entry_isPrimary(entry), isPrimary);
        CU_ASSERT_EQUAL(memcmp(uldb_entry_getSockAddr(entry), sockAddr,
                sizeof(*sockAddr)), 0);
        status = uldb_entry_getProdClass(entry, &allowed);
        CU_ASSERT_TRUE(clss_eq(&_clss_all, allowed));
        free_prod_class(allowed);
    }

    uldb_iter_free(iter);

    return pid;
}

static void test_add_feeder(void)
{
    struct sockaddr_in  sockAddr = new_sock_addr();
    pid_t               pid = set_uldb(6, &sockAddr, &_clss_all, 0, 1);
    int                 status;

    CU_ASSERT_EQUAL(get_size(), 1);

    CU_ASSERT_EQUAL(kill(pid, SIGTERM), 0);
    CU_ASSERT_EQUAL(waitpid(pid, &status, 0), pid);
    CU_ASSERT_TRUE(WIFEXITED(status));
    CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);

    CU_ASSERT_EQUAL(get_size(), 0);
}

static void test_add_same_feeder(void)
{
    int                 status;
    struct sockaddr_in  sockAddr = new_sock_addr();
    pid_t               pid = set_uldb(6, &sockAddr, &_clss_all, 0, 1);
    prod_class_t*       allowed;

    status = uldb_addProcess(pid, 6, &sockAddr, &_clss_all, &allowed, 0, 1);
    CU_ASSERT_EQUAL(status, ULDB_EXIST);
    log_clear();

    CU_ASSERT_EQUAL(get_size(), 1);
}

static void test_add_dup_feeder(void)
{
    int                 status;
    struct sockaddr_in  sockAddr = new_sock_addr();
    pid_t               pid1 = set_uldb(6, &sockAddr, &_clss_all, 0, 1);
    pid_t               pid2 = spawn_reg_upstream(6, &sockAddr, &_clss_all, 0, 1);

    CU_ASSERT_TRUE(pid1 > 0);
    CU_ASSERT_TRUE(pid2 > 0);

    sleep(1); /* allow "pid1" to exit */

    CU_ASSERT_EQUAL(waitpid(pid1, &status, 0), pid1);
    CU_ASSERT_TRUE(WIFEXITED(status));
    CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);
    CU_ASSERT_EQUAL(get_size(), 1);
}

static void test_add_notifier(void)
{
    struct sockaddr_in  sockAddr = new_sock_addr();

    CU_ASSERT_TRUE(set_uldb(6, &sockAddr, &_clss_all, 1, 0) > 0);
}

static void test_add_same_notifier(void)
{
    int                 status;
    struct sockaddr_in  sockAddr = new_sock_addr();
    pid_t               pid = set_uldb(6, &sockAddr, &_clss_all, 1, 0);
    prod_class_t*       allowed;

    status = uldb_addProcess(pid, 6, &sockAddr, &_clss_all, &allowed, 1, 0);
    CU_ASSERT_EQUAL(status, ULDB_EXIST);
    log_clear();

    CU_ASSERT_EQUAL(get_size(), 1);
}

static int set_independent(
        const int   isNotifier1,
        const int   isNotifier2)
{
    int                 success = 0; /* failure */
    struct sockaddr_in  sockAddr = new_sock_addr();
    pid_t               pid1 = set_uldb(6, &sockAddr, &_clss_all, isNotifier1, 1);

    if (pid1 <= 0) {
        LOG_ADD1("Couldn't set state to upstream LDM %s",
                isNotifier1 ? "notifier" : "feeder");
    }
    else {
        if (spawn_reg_upstream(6, &sockAddr, &_clss_all, isNotifier2, 1) <= 0) {
            LOG_ADD1("Couldn't spawn upstream LDM %s",
                    isNotifier2 ? "notifier" : "feeder");
        }
        else {
            unsigned    size;

            sleep(1);

            size = get_size();
            if (size != 2) {
                LOG_ADD1("Size is %u; not 2", size);
            }
            else {
                success = 1;
            }
        }
    }

    return success;
}

static void test_add_dup_notifier(void)
{
    log_clear();
    CU_ASSERT_TRUE(set_independent(1, 1));
    log_log(LOG_ERR);
}

static void test_add_feeder_and_notifier(void)
{
    log_clear();
    CU_ASSERT_TRUE(set_independent(0, 1));
    log_log(LOG_ERR);
}

/**
 * @param nchild        [in] The number of child processes. Too large a value
 *                      will bump into the {CHILD_MAX} limit (1024 on Gilda on
 *                      2013-04-26), resulting in an EAGAIN error. On
 *                      2013-04-26 on Gilda, the limit was 465.
 * @param duration      [in] How long, in seconds, to run the test.
 */
static void test_random(
        const unsigned  nchild,
        const unsigned  duration)
{
    unsigned    numChild = 0;

    clear();

    stop_time = time(NULL) + duration;

    for (;;) {
        int         status;
        pid_t       pid;
        int         done = time(NULL) >= stop_time;
        int         didNothing = 1;

        if (!done && get_size() < nchild) {
            pid = spawn_perf_upstream();

            if (pid <= 0) {
                LOG_ADD0("Couldn't spawn performance child");
                log_log(LOG_ERR);
                CU_ASSERT_TRUE(0);
            }
            else {
                numChild++;
                didNothing = 0;
            }
        }

        pid = waitpid(-1, &status, WNOHANG);
        if (pid == -1) {
            if (errno == ECHILD) {
                /* No child processes left */
                if (done) {
                    LOG_ADD1("Number of spawned processes = %u", numChild);
                    log_log(LOG_NOTICE);
                    return;
                }
            }
            else {
                LOG_SERROR0("waitpid() failure");
                log_log(LOG_ERR);
                CU_ASSERT_TRUE(0);
            }
        }
        else if (pid > 0) {
            CU_ASSERT_TRUE(WIFEXITED(status));
            CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);
            didNothing = 0;
        }

        if (didNothing)
            sleep(1);
    }
}

static void test_robustness(void)
{
    test_random(256, 5);
}

static void test_valgrind(
        void)
{
    test_random(5, 1);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1; /* failure */
    const char* progname = basename((char*) argv[0]);

    if (-1 == openulog(progname, LOG_PID, LOG_LOCAL0, "-")) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (argc >= 2
                        ? CU_ADD_TEST(testSuite, test_valgrind)
                        : (CU_ADD_TEST(testSuite, test_nil) &&
                           CU_ADD_TEST(testSuite, test_add_feeder) &&
                           CU_ADD_TEST(testSuite, test_add_same_feeder) &&
                           CU_ADD_TEST(testSuite, test_add_dup_feeder) &&
                           CU_ADD_TEST(testSuite, test_add_notifier) &&
                           CU_ADD_TEST(testSuite, test_add_same_notifier) &&
                           CU_ADD_TEST(testSuite, test_add_dup_notifier) &&
                           CU_ADD_TEST(testSuite, test_add_feeder_and_notifier) &&
                           CU_ADD_TEST(testSuite, test_robustness))) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                    exitCode = CU_get_error();
                }
            }

            CU_cleanup_registry();
        }
    }

    return exitCode;
}

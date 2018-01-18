/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `InAddrMgr` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "InAddrMgr.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>

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

static void test_immediate_clear(void)
{
    inam_clear();
}

static void test_inam_add(void)
{
    struct in_addr addr;
    feedtypet      feed = 1;
    CU_ASSERT_EQUAL(inam_add(feed, addr, 31), EINVAL);
    inet_pton(AF_INET, "192.168.128.0", &addr.s_addr);
    CU_ASSERT_EQUAL(inam_add(feed, addr, 8), EINVAL);
    CU_ASSERT_EQUAL(inam_add(feed, addr, 17), 0);
    char* logname = getenv("LOGNAME");
    if (logname == NULL)
        logname = getenv("USER");
    unsetenv("LOGNAME");
    unsetenv("USER");
    ++feed;
    CU_ASSERT_EQUAL(inam_add(feed, addr, 17), ENOENT);
    setenv("LOGNAME", logname, true);
    inam_clear();
}

static void test_inam_reserve(void)
{
    struct in_addr addr;
    addr.s_addr = inet_addr("192.168.0xff.0xfc");
    const feedtypet feed = 1;

    CU_ASSERT_EQUAL(inam_add(feed, addr, 30), 0);
    CU_ASSERT_EQUAL(inam_reserve(feed+1, &addr), ENOENT);
    struct in_addr addr2;

    CU_ASSERT_EQUAL(inam_reserve(feed, &addr2), 0);
    addr.s_addr = inet_addr("192.168.0xff.0xfd");
    CU_ASSERT_EQUAL(addr2.s_addr, addr.s_addr);

    CU_ASSERT_EQUAL(inam_reserve(feed, &addr2), 0);
    addr.s_addr = inet_addr("192.168.0xff.0xfe");
    CU_ASSERT_EQUAL(addr2.s_addr, addr.s_addr);

    CU_ASSERT_EQUAL(inam_reserve(feed, &addr2), EMFILE);

    inam_clear();
}

static void test_inam_reserve_parent(void)
{
    struct in_addr addr;
    addr.s_addr = inet_addr("192.168.0xff.0xfc");
    const feedtypet feed = 1;
    CU_ASSERT_EQUAL(inam_add(feed, addr, 30), 0);

    struct in_addr addr2;

    CU_ASSERT_EQUAL(inam_reserve(feed, &addr2), 0);
    addr.s_addr = inet_addr("192.168.0xff.0xfd");
    CU_ASSERT_EQUAL(addr2.s_addr, addr.s_addr);

    int forkPid = fork();
    CU_ASSERT_FATAL(forkPid >= 0);
    if (forkPid == 0) {
        CU_ASSERT_EQUAL(inam_reserve(feed, &addr2), 0);
        addr.s_addr = inet_addr("192.168.0xff.0xfe");
        CU_ASSERT_EQUAL(addr2.s_addr, addr.s_addr);
        exit(0);
    }
    else {
        int exitStatus;
        int childPid = wait(&exitStatus);
        CU_ASSERT_EQUAL(childPid, forkPid);
        CU_ASSERT_EQUAL(exitStatus, 0);
    }

    inam_clear();
}

static void handleSigTerm(int sig)
{}

static void test_inam_reserve_child(void)
{
    struct in_addr netAddr;
    netAddr.s_addr = inet_addr("192.168.0xff.0xfc");
    const feedtypet feed = 1;
    CU_ASSERT_EQUAL(inam_add(feed, netAddr, 30), 0);

    CU_ASSERT_EQUAL(sighold(SIGTERM), 0);
    CU_ASSERT_EQUAL(sigset(SIGTERM, handleSigTerm), SIG_HOLD);
    pid_t forkPid[2];
    for (int i = 0; i < 2; ++i) {
        forkPid[i] = fork();
        CU_ASSERT_NOT_EQUAL_FATAL(forkPid[i], -1);
        if (forkPid[i] == 0) {
            // Child process
            struct in_addr hostAddr;
            CU_ASSERT_EQUAL(inam_reserve(feed, &hostAddr), 0);
            CU_ASSERT_EQUAL(sigpause(SIGTERM), -1);
            exit((hostAddr.s_addr == inet_addr("192.168.0xff.0xfd"))
                    ? 1
                    : (hostAddr.s_addr == inet_addr("192.168.0xff.0xfe"))
                      ? 2
                      : 3);
        }
    }
    // Parent process
    usleep(1000); // Non-atomic fork() might erase child's pending SIGTERM
    CU_ASSERT_EQUAL(sigset(SIGTERM, SIG_DFL), handleSigTerm);
    CU_ASSERT_EQUAL(sigrelse(SIGTERM), 0);
    CU_ASSERT_EQUAL(kill(forkPid[0], SIGTERM), 0);
    CU_ASSERT_EQUAL(kill(forkPid[1], SIGTERM), 0);
    int exitStatus[2];
    for (int i = 0; i < 2; ++i) {
        pid_t pid = wait(exitStatus+i);
        CU_ASSERT_NOT_EQUAL(pid, -1);
        CU_ASSERT_TRUE(WIFEXITED(exitStatus[i]));
        CU_ASSERT_TRUE(WEXITSTATUS(exitStatus[i]) == 1
                    || WEXITSTATUS(exitStatus[i]) == 2);
    }
    CU_ASSERT_TRUE(WEXITSTATUS(exitStatus[0]) != WEXITSTATUS(exitStatus[1]));

    inam_clear();
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (log_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_immediate_clear)
                    && CU_ADD_TEST(testSuite, test_inam_add)
                    && CU_ADD_TEST(testSuite, test_inam_reserve_parent)
                    && CU_ADD_TEST(testSuite, test_inam_reserve_child)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    return exitCode;
}

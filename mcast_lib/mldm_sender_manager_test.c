/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file mldm_sender_test.c
 *
 * This file performs a unit-test of the multicast LDM sender.
 *
 * @author: Steven R. Emmerson
 */


#include "config.h"

#include "globals.h"
#include "ldm.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_manager.h"

#include "mldm_sender_map_stub.h"
#include "mcast_stub.h"
#include "pq_stub.h"
#include "unistd_stub.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

static const char* const    GROUP_ADDR = "224.0.0.1";
static const unsigned short GROUP_PORT = 1;
static const char* const    SERVER_ADDR = "192.168.0.1";
static const unsigned short SERVER_PORT = 38800;
static McastInfo*           mcastInfo;
static pid_t                mcastPid;
static pqueue*              prodQ = (void*)2;
static feedtypet            feedtype = 1;

static void
init()
{
    ServiceAddr* groupAddr;
    int          status = sa_new(&groupAddr, GROUP_ADDR, GROUP_PORT);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(groupAddr != NULL);
    ServiceAddr* serverAddr;
    status = sa_new(&serverAddr, SERVER_ADDR, SERVER_PORT);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(serverAddr != NULL);
    OP_ASSERT_EQUAL_INT(0, mi_new(&mcastInfo, feedtype, groupAddr, serverAddr));
    OP_ASSERT_TRUE(mcastInfo != NULL);
    sa_free(groupAddr);
    sa_free(serverAddr);
    prodQ = (void*)2;
}

static int
cmp_bool(void *a, void *b, const char * name, char * message)
{
    bool my_a = *(bool*)a;
    bool my_b = *(bool*)b;

    if (my_a == my_b)
        return 0;

    snprintf(message, OP_MATCHER_MESSAGE_LENGTH,
                   " parameter '%s' has value '%d', was expecting '%d'",
                   name, my_b, my_a);
    return -1;
}

static int msm_getPid_callback(
    const feedtypet feedtype,
    pid_t* const    pid,
    const int       callCount)
{
    *pid = mcastPid;
    return 0;
}

static void test_noPotentialSender()
{
    McastInfo* mcastInfo;
    OP_ASSERT_EQUAL_INT(LDM7_NOENT, mlsm_ensureRunning(feedtype, &mcastInfo));
    log_clear();
    OP_VERIFY();
}

static void test_conflict()
{
    // Depends on `init()`
    OP_ASSERT_EQUAL_INT(0, mlsm_addPotentialSender(mcastInfo));
    OP_ASSERT_EQUAL_INT(LDM7_DUP, mlsm_addPotentialSender(mcastInfo));
    OP_VERIFY();
}

static void test_not_running()
{
    // Depends on `test_conflict()`
    msm_lock_ExpectAndReturn(true, 0, cmp_bool);
    mcastPid = 1; // kill(1, 0) will return -1 -- emulating no-such-process
    msm_getPid_MockWithCallback(msm_getPid_callback);
    msm_removePid_ExpectAndReturn(mcastPid, 0, cmp_bool);
    fork_ExpectAndReturn(1);
    msm_put_ExpectAndReturn(feedtype, mcastPid, 0, cmp_int, cmp_int);
    msm_unlock_ExpectAndReturn(0);
    McastInfo* mcastInfo;
    OP_ASSERT_EQUAL_INT(0, mlsm_ensureRunning(feedtype, &mcastInfo));
    log_log(LOG_ERR);
    OP_VERIFY();
}

static void test_running()
{
    msm_lock_ExpectAndReturn(true, 0, cmp_bool);
    mcastPid = getpid(); // emulate running process
    msm_getPid_MockWithCallback(msm_getPid_callback);
    msm_unlock_ExpectAndReturn(0);
    McastInfo* mcastInfo;
    OP_ASSERT_EQUAL_INT(0, mlsm_ensureRunning(feedtype, &mcastInfo));
    log_log(LOG_ERR);
    OP_VERIFY();
}

int main(
    int		argc,
    char**	argv)
{
    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");
    (void) setulogmask(LOG_UPTO(LOG_NOTICE));
    opmock_test_suite_reset();
    opmock_register_test(test_noPotentialSender, "test_noPotentialSender");
    opmock_register_test(test_conflict, "test_conflict");
    opmock_register_test(test_not_running, "test_not_running");
    opmock_register_test(test_running, "test_running");
    init();
    opmock_test_suite_run();
    return opmock_get_number_of_errors();
}

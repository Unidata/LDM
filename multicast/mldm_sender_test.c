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

#include "ldm.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender.h"

#include "mldm_sender_memory_stub.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

static McastInfo*        mcastInfo;
static pid_t             mcastPid;
static MldmSenderMemory* msm = (void*)1;
static pqueue*           prodQ = (void*)2;

void
init()
{
    ServiceAddr* mcastAddr = sa_new("224.0.0.1", 1);
    OP_ASSERT_TRUE(mcastAddr != NULL);
    ServiceAddr* serverAddr = sa_new("192.168.0.1", 38800);
    OP_ASSERT_TRUE(serverAddr != NULL);
    mcastInfo = mi_new("group_name", mcastAddr, serverAddr);
    OP_ASSERT_TRUE(mcastInfo != NULL);
    sa_free(mcastAddr);
    sa_free(serverAddr);
    msm = (void*)1;
    prodQ = (void*)2;
}

static Ldm7Status msm_GetPid_callback(
    const MldmSenderMemory* const restrict msm,
    pid_t* const restrict                  pid,
    const int                              callCount)
{
    *pid = mcastPid;
    return 0;
}

void test_running()
{
    msm_new_ExpectAndReturn(mcastInfo, msm, cmp_ptr);
    msm_lock_ExpectAndReturn(msm, 0, cmp_ptr);
    mcastPid = getpid();
    msm_getPid_MockWithCallback(msm_GetPid_callback);
    msm_unlock_ExpectAndReturn(msm, 0, cmp_ptr);
    msm_free_ExpectAndReturn(msm, cmp_ptr);
    OP_ASSERT_TRUE(mls_ensureRunning(mcastInfo, prodQ) == 0);
    OP_VERIFY();
}

void test_not_running()
{
    msm_new_ExpectAndReturn(mcastInfo, msm, cmp_ptr);
    msm_lock_ExpectAndReturn(msm, 0, cmp_ptr);
    mcastPid = 1; // kill(1, 0) will return -1 -- emulating no-such-process
    msm_getPid_MockWithCallback(msm_GetPid_callback);
    msm_setPid_ExpectAndReturn(msm, 1, 0, cmp_ptr, NULL);
    msm_unlock_ExpectAndReturn(msm, 0, cmp_ptr);
    msm_free_ExpectAndReturn(msm, cmp_ptr);
    OP_ASSERT_TRUE(mls_ensureRunning(mcastInfo, prodQ) == 0);
    OP_VERIFY();
}

void test_mls_execute()
{
    OP_ASSERT_TRUE(mls_execute(mcastInfo, prodQ) == 0);
    OP_VERIFY();
}

int main(
    int		argc,
    char**	argv)
{
    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");
    (void) setulogmask(LOG_UPTO(LOG_NOTICE));
    opmock_test_suite_reset();
    opmock_register_test(test_running, "test_running");
    opmock_register_test(test_not_running, "test_not_running");
    // opmock_register_test(test_mls_execute, "test_mls_execute");
    init();
    opmock_test_suite_run();
    return opmock_get_number_of_errors();
}

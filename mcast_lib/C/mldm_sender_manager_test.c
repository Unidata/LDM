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
#include "inetutil.h"
#include "ldm.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_manager.h"
#include "mldm_sender_map.h"

#include "mcast_stub.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

static const char* const    GROUP_ADDR = "224.0.0.1";
static const unsigned short GROUP_PORT = 1;
static const char* const    SERVER_ADDR = "0.0.0.0";
static const unsigned short SERVER_PORT = 38800;
static McastInfo*           mcastInfo;
static feedtypet            feedtype = 1;
static ServiceAddr*         groupAddr;
static ServiceAddr*         serverAddr;

static void
init()
{
    int          status = sa_new(&groupAddr, GROUP_ADDR, GROUP_PORT);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(groupAddr != NULL);
    status = sa_new(&serverAddr, SERVER_ADDR, SERVER_PORT);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(serverAddr != NULL);
    status = mi_new(&mcastInfo, feedtype, groupAddr, serverAddr);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(mcastInfo != NULL);
    status = msm_init();
    OP_ASSERT_EQUAL_INT(0, status);
}

static void
test_noPotentialSender()
{
    const McastInfo* mcastInfo;
    pid_t            pid;
    int              status = mlsm_ensureRunning(feedtype, &mcastInfo,
            &pid);
    OP_ASSERT_EQUAL_INT(LDM7_NOENT, status);
    log_clear();
    OP_VERIFY();
}

static void
test_conflict()
{
    // Depends on `init()`
    int status = mlsm_addPotentialSender(mcastInfo);
    OP_ASSERT_EQUAL_INT(0, status);
    status = mlsm_addPotentialSender(mcastInfo);
    OP_ASSERT_EQUAL_INT(LDM7_DUP, status);
    log_clear();
    OP_VERIFY();
}

static void
test_not_running()
{
    // Depends on `test_conflict()`
    const McastInfo* mcastInfo;
    pid_t            pid;
    int              status;

    /* Start a multicast sender process */
    status = mlsm_ensureRunning(feedtype, &mcastInfo, &pid);
    log_log(LOG_ERR);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype, mcastInfo->feed);
    OP_ASSERT_EQUAL_INT(0, sa_compare(groupAddr, &mcastInfo->group));
    OP_ASSERT_EQUAL_INT(0, sa_compare(serverAddr, &mcastInfo->server));
    OP_ASSERT_TRUE(pid > 0);

    /* Terminate the multicast sender process */
    status = kill(pid, SIGTERM);
    OP_ASSERT_EQUAL_INT(0, status);
    int childStatus;
    status = waitpid(pid, &childStatus, 0);
    OP_ASSERT_EQUAL_INT(pid, status);
    if (WIFEXITED(childStatus)) {
        OP_ASSERT_EQUAL_INT(0, WEXITSTATUS(childStatus));
    }
    else {
        OP_ASSERT_TRUE(WIFSIGNALED(childStatus));
        OP_ASSERT_EQUAL_INT(SIGTERM, WTERMSIG(childStatus));
    }

    OP_VERIFY();
}

static void
test_running()
{
    // Depends on `test_conflict()`
    const McastInfo* mcastInfo;
    pid_t            pid;
    int              status;

    /* Start a multicast sender */
    status = mlsm_ensureRunning(feedtype, &mcastInfo, &pid);
    log_log(LOG_ERR);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype, mcastInfo->feed);
    OP_ASSERT_EQUAL_INT(0, sa_compare(groupAddr, &mcastInfo->group));
    OP_ASSERT_EQUAL_INT(0, sa_compare(serverAddr, &mcastInfo->server));
    OP_ASSERT_TRUE(pid > 0);

    /* Try starting a duplicate multicast sender */
    pid_t      pid2;
    status = mlsm_ensureRunning(feedtype, &mcastInfo, &pid2);
    log_log(LOG_ERR);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype, mcastInfo->feed);
    status = sa_compare(groupAddr, &mcastInfo->group);
    OP_ASSERT_EQUAL_INT(0, status);
    status = sa_compare(serverAddr, &mcastInfo->server);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(pid, pid2);

    /* Terminate the multicast sender */
    status = kill(pid, SIGTERM);
    OP_ASSERT_EQUAL_INT(0, status);
    int childStatus;
    status = waitpid(pid, &childStatus, 0);
    OP_ASSERT_EQUAL_INT(pid, status);
    if (WIFEXITED(childStatus)) {
        OP_ASSERT_EQUAL_INT(0, WEXITSTATUS(childStatus));
    }
    else {
        OP_ASSERT_TRUE(WIFSIGNALED(childStatus));
        OP_ASSERT_EQUAL_INT(SIGTERM, WTERMSIG(childStatus));
    }

    OP_VERIFY();
}

int
main(
    int		argc,
    char**	argv)
{
    int status;

    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");
    (void) setulogmask(LOG_UPTO(LOG_NOTICE));

    if (NULL == argv[1]) {
        uerror("Product-queue pathname not given");
        status = 1;
    }
    else {
        setQueuePath(argv[1]);
        opmock_test_suite_reset();
        opmock_register_test(test_noPotentialSender, "test_noPotentialSender");
        opmock_register_test(test_conflict, "test_conflict");
        opmock_register_test(test_not_running, "test_not_running");
        opmock_register_test(test_running, "test_running");
        init();
        opmock_test_suite_run();
        status = opmock_test_error ? 1 : 0;
    }

    return status;
}

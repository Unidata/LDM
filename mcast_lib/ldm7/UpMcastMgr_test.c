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
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mcast_stub.h"
#include "mldm_sender_map.h"
#include "pq.h"
#include "UpMcastMgr.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <opmock.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

#define PQ_PATHNAME "mldm_sender_manager_test.pq"

static const char* const    GROUP_ADDR_1 = "224.0.0.1";
static const char* const    GROUP_ADDR_2 = "224.0.0.2";
static const unsigned short GROUP_PORT_1 = 1;
static const unsigned short GROUP_PORT_2 = 2;
static const char* const    SERVER_ADDR = "127.0.0.1";
static McastInfo*           mcastInfo_1;
static McastInfo*           mcastInfo_2;
static feedtypet            feedtype_1 = 1;
static feedtypet            feedtype_2 = 2;
static ServiceAddr*         groupAddr_1;
static ServiceAddr*         groupAddr_2;
static ServiceAddr*         serverAddr_1;
static ServiceAddr*         serverAddr_2;

static void
init()
{
    int          status = sa_new(&groupAddr_1, GROUP_ADDR_1, GROUP_PORT_1);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(groupAddr_1 != NULL);
    status = sa_new(&groupAddr_2, GROUP_ADDR_2, GROUP_PORT_2);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(groupAddr_2 != NULL);
    status = sa_new(&serverAddr_1, SERVER_ADDR, 0); // O/S chooses port number
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(serverAddr_1 != NULL);
    status = sa_new(&serverAddr_2, SERVER_ADDR, 0); // O/S chooses port number
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(serverAddr_2 != NULL);
    status = mi_new(&mcastInfo_1, feedtype_1, groupAddr_1, serverAddr_1);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(mcastInfo_1 != NULL);
    status = mi_new(&mcastInfo_2, feedtype_2, groupAddr_2, serverAddr_2);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_TRUE(mcastInfo_2 != NULL);
    status = msm_init();
    OP_ASSERT_EQUAL_INT(0, status);
    status = pq_create(PQ_PATHNAME, S_IRUSR|S_IWUSR, 0, 0, 1000000, 1000, &pq);
    OP_ASSERT_EQUAL_INT(0, status);
    char* path = ldm_format(256, "%s:%s", "../../mldm_sender", getenv("PATH"));
    OP_ASSERT_TRUE(path != NULL);
    status = setenv("PATH", path, 1);
    OP_ASSERT_EQUAL_INT(0, status);
    free(path);
}

static void
destroy(void)
{
    int status = unlink(PQ_PATHNAME);
    OP_ASSERT_EQUAL_INT(0, status);
}

static void
test_noPotentialSender()
{
    const McastInfo* mcastInfo;
    pid_t            pid;
    int              status = umm_subscribe(feedtype_1, &mcastInfo,
            &pid);
    OP_ASSERT_EQUAL_INT(LDM7_NOENT, status);
    log_clear();
    OP_VERIFY();
}

static void
test_conflict()
{
    // Depends on `init()`
    int status = umm_addPotentialSender(mcastInfo_1, 0, NULL, PQ_PATHNAME);
    OP_ASSERT_EQUAL_INT(0, status);
    status = umm_addPotentialSender(mcastInfo_1, 0, NULL, PQ_PATHNAME);
    OP_ASSERT_EQUAL_INT(LDM7_DUP, status);
    status = umm_addPotentialSender(mcastInfo_2, 0, NULL, PQ_PATHNAME);
    OP_ASSERT_EQUAL_INT(0, status);
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
    status = pim_delete(NULL, feedtype_1);
    OP_ASSERT_EQUAL_INT(0, status);
    status = umm_subscribe(feedtype_1, &mcastInfo, &pid);
    log_flush_error();
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype_1, mcastInfo->feed);
    OP_ASSERT_EQUAL_INT(0, sa_compare(groupAddr_1, &mcastInfo->group));
    OP_ASSERT_EQUAL_INT(0, strcmp(sa_getInetId(serverAddr_1),
            sa_getInetId(&mcastInfo->server)));
    OP_ASSERT_NOT_EQUAL_USHORT(0, sa_getPort(&mcastInfo->server));
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
    status = pim_delete(NULL, feedtype_1);
    OP_ASSERT_EQUAL_INT(0, status);
    status = umm_subscribe(feedtype_1, &mcastInfo, &pid);
    log_flush_error();
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype_1, mcastInfo->feed);
    OP_ASSERT_EQUAL_INT(0, sa_compare(groupAddr_1, &mcastInfo->group));
    OP_ASSERT_EQUAL_INT(0, strcmp(sa_getInetId(serverAddr_1),
            sa_getInetId(&mcastInfo->server)));
    OP_ASSERT_NOT_EQUAL_USHORT(0, sa_getPort(&mcastInfo->server));
    OP_ASSERT_TRUE(pid > 0);

    /* Try starting a duplicate multicast sender */
    pid_t      pid2;
    status = umm_subscribe(feedtype_1, &mcastInfo, &pid2);
    log_flush_error();
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(feedtype_1, mcastInfo->feed);
    status = sa_compare(groupAddr_1, &mcastInfo->group);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_INT(0, strcmp(sa_getInetId(serverAddr_1),
            sa_getInetId(&mcastInfo->server)));
    OP_ASSERT_NOT_EQUAL_USHORT(0, sa_getPort(&mcastInfo->server));
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
    (void)log_init(argv[0]);
    (void)log_set_level(LOG_LEVEL_NOTICE);

    opmock_test_suite_reset();
    opmock_register_test(test_noPotentialSender, "test_noPotentialSender");
    opmock_register_test(test_conflict, "test_conflict");
    opmock_register_test(test_not_running, "test_not_running");
    opmock_register_test(test_running, "test_running");

    init();
    opmock_test_suite_run();
    destroy();

    return opmock_test_error ? 1 : 0;
}

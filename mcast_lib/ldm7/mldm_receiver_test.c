/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file mldm_receiver_test.c
 *
 * This file performs a unit-test of the mldm_receiver module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "down7_stub.h"
#include "mcast_stub.h"
#include "mldm_receiver.h"
#include "pq_stub.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <stdlib.h>

#include "../ldm7/mcast_info.h"

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT"
#endif

#if 0
static void missed_product_func(
    const FmtpFileId fileId)
{
}
#endif

static const char* const    LOOPBACK_IP = "127.0.0.1";
static const char* const    mcastAddr = "224.0.0.1";
static const unsigned short mcastPort = 1;
static const char* const    ucastAddr = "127.0.0.1";
static const unsigned short ucastPort = 38800;
static ServiceAddr*         mcastSa;
static ServiceAddr*         ucastSa;
static McastInfo*           mcastInfo;
static int                  (*const int_func)() = (int(*)())1;
static void                 (*const void_func)() = (void(*)())2;
static Down7* const         down7 = (Down7*)1;

static void
init(void)
{
    int status;

    status = sa_new(&mcastSa, mcastAddr, mcastPort);
    OP_ASSERT_EQUAL_INT(0, status);

    status = sa_new(&ucastSa, ucastAddr, ucastPort);
    OP_ASSERT_EQUAL_INT(0, status);

    status = mi_new(&mcastInfo, IDS|DDPLUS, mcastSa, ucastSa);
    OP_ASSERT_EQUAL_INT(0, status);
}

void
test_invalidMcastInfo()
{
    int                  status;
    Mlr*                 mlr;

    /* Invalid multicast information argument */
    mlr = mlr_new(NULL, LOOPBACK_IP, down7);
    log_clear();
    OP_ASSERT_TRUE(mlr == NULL);
    OP_VERIFY();
}

void
test_invalidDown7()
{
    int                  status;
    Mlr*                 mlr;

    /* Invalid multicast information argument */
    mlr = mlr_new(mcastInfo, LOOPBACK_IP, NULL);
    log_clear();
    OP_ASSERT_TRUE(mlr == NULL);
    OP_VERIFY();
}

void
test_trivialExecution()
{
    int                  status;
    Mlr*                 mlr;
    pqueue*              pq = (pqueue*)5;

    /* Trivial execution */
    mcastReceiver_new_ExpectAndReturn(
            NULL, ucastAddr, ucastPort, NULL, mcastAddr, mcastPort, LOOPBACK_IP, 0,
            NULL, cmp_cstr,  cmp_short, NULL, cmp_cstr,  cmp_short, cmp_cstr);
    down7_getPq_ExpectAndReturn(down7, pq,
                                cmp_ptr);
    mlr = mlr_new(mcastInfo, LOOPBACK_IP, down7);
    log_flush_error();
    OP_ASSERT_TRUE(mlr != NULL);
    mcastReceiver_free_ExpectAndReturn(NULL, NULL);
    mlr_free(mlr);
    OP_VERIFY();
}

#if 0
void
test_mdl_createAndExecute()
{
    int                  status;
    Mlr*                 mdl;


    fmtpReceiver_execute_ExpectAndReturn(NULL, 0, NULL);
    status = mlr_start(mdl);
    log_flush_info();
    OP_ASSERT_EQUAL_INT(LDM7_SHUTDOWN, status);

    fmtpReceiver_free_ExpectAndReturn(NULL, NULL);
    mlr_free(mdl);
    log_flush_info();

    OP_VERIFY();
}
#endif

int main(
    int		argc,
    char**	argv)
{
    (void)log_init(argv[0]);
    (void)log_set_level(LOG_LEVEL_NOTICE);
    opmock_test_suite_reset();
    opmock_register_test(test_invalidMcastInfo, "test_invalidMcastInfo");
    opmock_register_test(test_trivialExecution, "test_trivialExecution");
    //opmock_register_test(test_mdl_createAndExecute, "test_mdl_createAndExecute");
    init();
    opmock_test_suite_run();
    return opmock_test_error ? 1 : 0;
}

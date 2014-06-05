/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file mcast_down_test.c
 *
 * This file performs a unit-test of the mcast_down module.
 *
 * @author: Steven R. Emmerson
 */


#include "config.h"

#include "ldm.h"
#include "log.h"
#include "mcast_down.h"

#include "vcmtp_c_api_stub.h"
#include "pq_stub.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <stdlib.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT"
#endif

static void missed_product_func(
    const VcmtpFileId fileId)
{
}

void test_mdl_createAndExecute()
{
    int                  status;
    pqueue*              pq = (pqueue*)1;
    char* const          tcpAddr = "127.0.0.1";
    const unsigned short tcpPort = 38800;
    char* const          addr = "224.0.0.1";
    const unsigned short port = 1;
    int                  (*int_func)() = (int(*)())1;
    void                 (*void_func)() = (void(*)())2;
    McastGroupInfo       mcastInfo;
    Mdl*                 mdl;

    mcastInfo.mcastAddr = addr;
    mcastInfo.mcastPort = port;
    mcastInfo.tcpAddr = tcpAddr;
    mcastInfo.tcpPort = tcpPort;

    /* Invalid product-queue argument */
    mdl = mdl_new(NULL, &mcastInfo, void_func, NULL);
    log_log(LOG_INFO);
    OP_ASSERT_TRUE(mdl == NULL);
    log_clear();

    /* Invalid multicast information argument */
    mdl = mdl_new(pq, NULL, void_func, NULL);
    log_log(LOG_INFO);
    OP_ASSERT_TRUE(mdl == NULL);
    log_clear();

    /* Invalid missed-product-function argument */
    mdl = mdl_new(pq, &mcastInfo, NULL, NULL);
    log_log(LOG_INFO);
    OP_ASSERT_TRUE(mdl == NULL);
    log_clear();

    /* Trivial execution */
    vcmtpReceiver_new_ExpectAndReturn(
            NULL, tcpAddr,  tcpPort,   int_func, int_func, void_func, addr,     port,      NULL,   0,
            NULL, cmp_cstr, cmp_short, NULL,     NULL,     NULL,      cmp_cstr, cmp_short, NULL);
    mdl = mdl_new(pq, &mcastInfo, void_func, NULL);
    log_log(LOG_INFO);
    OP_ASSERT_FALSE(mdl == NULL);

    vcmtpReceiver_execute_ExpectAndReturn(NULL, 0, NULL);
    status = mdl_start(mdl);
    log_log(LOG_INFO);
    OP_ASSERT_EQUAL_INT(LDM7_CANCELED, status);

    vcmtpReceiver_free_ExpectAndReturn(NULL, NULL);
    mdl_free(mdl);
    log_log(LOG_INFO);

    OP_VERIFY();
}

int main(
    int		argc,
    char**	argv)
{
    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");
    (void) setulogmask(LOG_UPTO(LOG_NOTICE));
    opmock_test_suite_reset();
    opmock_register_test(test_mdl_createAndExecute, "test_mdl_createAndExecute");
    opmock_test_suite_run();
    return opmock_get_number_of_errors();
}

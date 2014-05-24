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

    mcastInfo.mcastAddr = addr;
    mcastInfo.mcastPort = port;
    mcastInfo.tcpAddr = tcpAddr;
    mcastInfo.tcpPort = tcpPort;

    /* Invalid product-queue argument */
    status = mdl_createAndExecute(NULL, (void*)1, &mcastInfo);
    log_log(LOG_INFO);
    OP_ASSERT_EQUAL_INT(LDM7_INVAL, status);
    log_clear();

    /* Invalid missed-product-function argument */
    status = mdl_createAndExecute((void*)1, NULL, &mcastInfo);
    log_log(LOG_INFO);
    OP_ASSERT_EQUAL_INT(LDM7_INVAL, status);
    log_clear();

    /* Invalid multicast IP address argument */
    status = mdl_createAndExecute((void*)1, (void*)1, NULL);
    log_log(LOG_INFO);
    OP_ASSERT_EQUAL_INT(LDM7_INVAL, status);
    log_clear();

    vcmtpReceiver_new_ExpectAndReturn(
    /* Trivial execution */
            NULL, tcpAddr,  tcpPort,   int_func, int_func, void_func, addr,     port,      NULL,   0,
            NULL, cmp_cstr, cmp_short, NULL,     NULL,     NULL,      cmp_cstr, cmp_short, NULL);
    vcmtpReceiver_execute_ExpectAndReturn(NULL, 0, NULL);
    vcmtpReceiver_free_ExpectAndReturn(NULL, NULL);
    status = mdl_createAndExecute(pq, missed_product_func, &mcastInfo);
    log_log(LOG_INFO);
    OP_ASSERT_EQUAL_INT(0, status);
    log_clear();

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

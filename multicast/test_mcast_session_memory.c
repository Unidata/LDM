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

#include "globals_stub.h"
#include "inetutil.h"
#include "ldm.h"
#include "log.h"
#include "mcast_session_memory.h"

#include <errno.h>
#include <libgen.h>
#include <opmock.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT"
#endif

static void openMsm(
    McastSessionMemory** msm)
{
    char                buf[265];
    const char* const   cwd = getcwd(buf, sizeof(buf));
    const void* const   servAddr = sa_new("hostname", 38800);

    OP_ASSERT_TRUE(servAddr != NULL);
    getLdmLogDir_ExpectAndReturn(cwd);
    *msm = msm_open(servAddr, "mcast-group-id");
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(*msm != NULL);
}

static void test_missed_mcast_files()
{

}

static void test_last_mcast_prod()
{
    McastSessionMemory* msm;
    int                 status;

    openMsm(&msm);

    signaturet sig1;
    status = msm_getLastMcastProd(msm, sig1);
    log_log(LOG_ERR);
    OP_ASSERT_FALSE(status);

    signaturet sig2;
    (void)memset(&sig2, 1, sizeof(sig2));
    status = msm_setLastMcastProd(msm, sig2);
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(status);
    status = msm_getLastMcastProd(msm, sig1);
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(status);
    OP_ASSERT_TRUE(memcmp(&sig1, &sig2, sizeof(sig1)) == 0);

    status = msm_close(msm);
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(status);

#if 0
    // Verify the data in the new file

    openMsm(&msm);

    status = msm_getLastMcastProd(msm, sig1);
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(status);
    OP_ASSERT_TRUE(memcmp(&sig1, &sig2, sizeof(signaturet)) == 0);

    status = msm_close(msm);
    log_log(LOG_ERR);
    OP_ASSERT_TRUE(msm_close(msm));
#endif

    OP_VERIFY();
}

static void test_msm_open()
{
    McastSessionMemory* msm;

    openMsm(&msm);
    OP_ASSERT_TRUE(msm != NULL);

    OP_ASSERT_TRUE(msm_close(msm));
    log_log(LOG_ERR);

    OP_VERIFY();

#if 0
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
#endif
}

int main(
    int		argc,
    char**	argv)
{
    (void) openulog(basename(argv[0]), LOG_NOTIME | LOG_IDENT, LOG_LDM, "-");
    (void) setulogmask(LOG_UPTO(LOG_INFO));
    opmock_test_suite_reset();
    opmock_register_test(test_msm_open, "test_msm_open");
    opmock_register_test(test_last_mcast_prod, "test_last_mcast_prod");
    opmock_test_suite_run();
    return opmock_get_number_of_errors();
}

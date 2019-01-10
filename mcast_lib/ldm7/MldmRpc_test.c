/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `MldmRpc` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "CidrAddr.h"
#include "log.h"
#include "MldmRpc.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <libgen.h>
#include <pthread.h>
#include <stdbool.h>

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

static void test_construction(void)
{
    in_addr_t networkPrefix;
    CU_ASSERT_EQUAL(inet_pton(AF_INET, "192.168.0.0", &networkPrefix), 1);
    CidrAddr* subnet = cidrAddr_new(networkPrefix, 16);
    void* fmtpClntAddrs = fmtpClntAddrs_new(subnet);
    CU_ASSERT_PTR_NOT_NULL_FATAL(fmtpClntAddrs);
    void* mldmSrvr = mldmSrvr_new(fmtpClntAddrs);
    CU_ASSERT_PTR_NOT_NULL_FATAL(mldmSrvr);
    CU_ASSERT_TRUE(0 < mldmSrvr_getPort(mldmSrvr));
    mldmSrvr_free(mldmSrvr);
    fmtpClntAddrs_free(fmtpClntAddrs);
    cidrAddr_free(subnet);
}

static void* runServer(void* mldmSrvr)
{
    if (mldmSrvr_run(mldmSrvr))
        log_error_q("Multicast LDM RPC server returned");
    return NULL;
}

static void test_reserveAndRelease(void)
{
    int status;

    in_addr_t networkPrefix;
    CU_ASSERT_EQUAL(inet_pton(AF_INET, "1.0.0.0", &networkPrefix), 1);
    CidrAddr* subnet = cidrAddr_new(networkPrefix, 24);
    void* fmtpClntAddrs = fmtpClntAddrs_new(subnet);
    void* mldmSrvr = mldmSrvr_new(fmtpClntAddrs);
    in_port_t port = mldmSrvr_getPort(mldmSrvr);
    pthread_t mldmSrvrThread;
    pthread_create(&mldmSrvrThread, NULL, runServer, mldmSrvr);

    void* mldmClnt = mldmClnt_new(port);
    CU_ASSERT_PTR_NOT_NULL_FATAL(mldmClnt);
    in_addr_t fmtpAddr = 0;
    CU_ASSERT_EQUAL(mldmClnt_reserve(mldmClnt, &fmtpAddr), 0);
    CU_ASSERT_NOT_EQUAL(fmtpAddr, 0);
    CU_ASSERT_TRUE(cidrAddr_isMember(subnet, fmtpAddr));
    CU_ASSERT_TRUE(fmtpClntAddrs_isAllowed(fmtpClntAddrs, fmtpAddr));
    CU_ASSERT_EQUAL(mldmClnt_release(mldmClnt, fmtpAddr), 0);
    CU_ASSERT_FALSE(fmtpClntAddrs_isAllowed(fmtpClntAddrs, fmtpAddr));

    mldmClnt_free(mldmClnt);
    status = mldmSrvr_stop(mldmSrvr);
    CU_ASSERT_EQUAL(status, 0);
    void* ptr;
    status = pthread_join(mldmSrvrThread, &ptr);
    CU_ASSERT_EQUAL(status, 0);
    mldmSrvr_free(mldmSrvr);
    fmtpClntAddrs_free(fmtpClntAddrs);
    cidrAddr_free(subnet);
}

static void test_releaseUnreserved(void)
{
    int status;

    in_addr_t networkPrefix;
    CU_ASSERT_EQUAL(inet_pton(AF_INET, "192.168.0.0", &networkPrefix), 1);
    CidrAddr* subnet = cidrAddr_new(networkPrefix, 16);
    void* fmtpClntAddrs = fmtpClntAddrs_new(subnet);
    void* mldmSrvr = mldmSrvr_new(fmtpClntAddrs);
    in_port_t port = mldmSrvr_getPort(mldmSrvr);
    pthread_t thread;
    pthread_create(&thread, NULL, runServer, mldmSrvr);

    void* mldmClnt = mldmClnt_new(port);
    in_addr_t fmtpAddr;
    CU_ASSERT_EQUAL(inet_pton(AF_INET, "192.168.0.1", &fmtpAddr), 1);
    CU_ASSERT_FALSE(fmtpClntAddrs_isAllowed(fmtpClntAddrs, fmtpAddr));
    CU_ASSERT_EQUAL(mldmClnt_release(mldmClnt, fmtpAddr), LDM7_NOENT);
    log_notice_q("");

    mldmClnt_free(mldmClnt);
    status = mldmSrvr_stop(mldmSrvr);
    CU_ASSERT_EQUAL(status, 0);
    void* ptr;
    status = pthread_join(thread, &ptr);
    CU_ASSERT_EQUAL(status, 0);
    mldmSrvr_free(mldmSrvr);
    fmtpClntAddrs_free(fmtpClntAddrs);
    cidrAddr_free(subnet);
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
                if (CU_ADD_TEST(testSuite, test_construction)
                    && CU_ADD_TEST(testSuite, test_reserveAndRelease)
                    && CU_ADD_TEST(testSuite, test_releaseUnreserved)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_suites_failed() +
                    CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    log_fini();
    return exitCode;
}

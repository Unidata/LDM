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

#include "Authorizer.h"
#include "log.h"
#include "MldmRpc.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdbool.h>
#include <pthread.h>

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

static void test_defaultConstruction(void)
{
    void* authDb = auth_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(authDb);
    void* mldmSrvr = mldmSrvr_new(authDb);
    CU_ASSERT_PTR_NOT_NULL_FATAL(mldmSrvr);
    CU_ASSERT_TRUE(0 < mldmSrvr_getPort(mldmSrvr));
    mldmSrvr_delete(mldmSrvr);
    auth_delete(authDb);
}

static void* runServer(void* mldmSrvr)
{
    if (mldmSrvr_run(mldmSrvr))
        log_error("Multicast LDM RPC server returned");
    return NULL;
}

static void test_reserving(void)
{
    void* authDb = auth_new();
    void* mldmSrvr = mldmSrvr_new(authDb);
    in_port_t port = mldmSrvr_getPort(mldmSrvr);
    pthread_t thread;
    pthread_create(&thread, NULL, runServer, mldmSrvr);
    pthread_detach(thread);

    void* mldmClnt = mldmClnt_new(port);
    CU_ASSERT_PTR_NOT_NULL_FATAL(mldmClnt);
    struct in_addr fmtpAddr = {};
    CU_ASSERT_TRUE(0 == mldmClnt_reserve(mldmClnt, &fmtpAddr));
    CU_ASSERT_TRUE(0 != fmtpAddr.s_addr);

    mldmClnt_delete(mldmClnt);
    mldmSrvr_delete(mldmSrvr);
    auth_delete(authDb);
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
                if (CU_ADD_TEST(testSuite, test_defaultConstruction)
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

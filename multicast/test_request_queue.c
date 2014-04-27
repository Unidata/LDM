/*
 * Copyright 2014 University Corporation for Atmospheric Research.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */
#include "config.h"

#include "request_queue.h"
#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <string.h>
#include <unistd.h>

static RequestQueue* requestQueue;

static int
setup(void)
{
    if (NULL == (requestQueue = rq_new())) {
        (void)fprintf(stderr, "Couldn't create request-queue\n");
        return -1;
    }
    return 0;
}

static int
teardown(void)
{
    rq_free(requestQueue);
    return 0;
}

static void
test_allocation(void)
{
    RequestQueue* rq = rq_new();

    CU_ASSERT_PTR_NOT_NULL_FATAL(rq);
}

int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;

    if (-1 == openulog(basename(argv[0]), 0, LOG_LOCAL0, "-")) {
        (void)fprintf(stderr, "Couldn't initialize logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite*       testSuite = CU_add_suite(__FILE__, setup,
                teardown);

            if (NULL != testSuite) {
                CU_ADD_TEST(testSuite, test_allocation);

                if (CU_basic_run_tests() == CUE_SUCCESS)
                    exitCode = CU_get_number_of_failures();
            }

            CU_cleanup_registry();
        } /* CUnit registery allocated */
    } /* logging system initialized */

    return exitCode;
}

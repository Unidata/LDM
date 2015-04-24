/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests asynchronous jobs.
 */

#include "config.h"

#include "queue.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

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

static void test_q_new(void)
{
    Queue* q = q_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(q);
    CU_ASSERT_PTR_NULL(q_dequeue(q));
    CU_ASSERT_EQUAL(q_size(q), 0);
    q_free(q);
}

static void test_q_enqueue(void)
{
    Queue* q = q_new();
    int first;
    int status = q_enqueue(q, &first);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(q_size(q), 1);
    int second;
    status = q_enqueue(q, &second);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(q_size(q), 2);
    q_free(q);
}

static void test_q_dequeue(void)
{
    Queue* q = q_new();
    int first;
    int status = q_enqueue(q, &first);
    int second;
    status = q_enqueue(q, &second);
    CU_ASSERT_PTR_EQUAL(q_dequeue(q), &first);
    CU_ASSERT_EQUAL(q_size(q), 1);
    CU_ASSERT_PTR_EQUAL(q_dequeue(q), &second);
    CU_ASSERT_EQUAL(q_size(q), 0);
    q_free(q);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_q_new) &&
                    CU_ADD_TEST(testSuite, test_q_enqueue) &&
                    CU_ADD_TEST(testSuite, test_q_dequeue)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}

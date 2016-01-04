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

#include "doubly_linked_list.h"
#include "mylog.h"

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

static void test_dll_new(void)
{
    Dll* list = dll_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(list);
    CU_ASSERT_PTR_NULL(dll_getFirst(list));
    CU_ASSERT_EQUAL(dll_size(list), 0);
    dll_free(list);
}

static void test_dll_add_null(void)
{
    Dll* list = dll_new();
    DllElt* elt = dll_add(list, NULL);
    mylog_clear();
    CU_ASSERT_PTR_NULL(elt);
    CU_ASSERT_EQUAL(dll_size(list), 0);
    dll_free(list);
}

static void test_dll_add(void)
{
    Dll* list = dll_new();
    int value;
    DllElt* elt = dll_add(list, &value);
    CU_ASSERT_PTR_NOT_NULL(elt);
    CU_ASSERT_EQUAL(dll_size(list), 1);
    dll_free(list);
}

static void test_dll_getFirst(void)
{
    Dll* list = dll_new();
    int value;
    (void)dll_add(list, &value);
    void* valuePtr = dll_getFirst(list);
    CU_ASSERT_PTR_EQUAL(valuePtr, &value);
    CU_ASSERT_EQUAL(dll_size(list), 0);
    dll_free(list);
}

static void test_dll_remove(void)
{
    Dll* list = dll_new();
    int value;
    DllElt* elt = dll_add(list, &value);
    void* valuePtr = dll_remove(list, elt);
    CU_ASSERT_PTR_EQUAL(valuePtr, &value);
    CU_ASSERT_EQUAL(dll_size(list), 0);
    dll_free(list);
}

static void test_dll_iter(void)
{
    Dll* list = dll_new();
    #define NELT 2
    int values[NELT];
    for (int i = 0; i < NELT; i++)
        (void)dll_add(list, values+i);
    DllIter* iter = dll_iter(list);
    for (int i = 0; dll_hasNext(iter); i++)
        CU_ASSERT_PTR_EQUAL(dll_next(iter), values+i);
    dll_freeIter(iter);
    dll_free(list);
}

int main(
        const int argc,
        const char* const * argv)
{
    int         exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (mylog_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_dll_new) &&
                        CU_ADD_TEST(testSuite, test_dll_add_null) &&
                        CU_ADD_TEST(testSuite, test_dll_add) &&
                        CU_ADD_TEST(testSuite, test_dll_getFirst) &&
                        CU_ADD_TEST(testSuite, test_dll_remove) &&
                        CU_ADD_TEST(testSuite, test_dll_iter)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }

        mylog_free();
    }

    return exitCode;
}

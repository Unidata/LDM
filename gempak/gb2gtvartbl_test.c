/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: gb2gtvartbl_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the `gb2_gtvartbl()` function against a mock of
 * `ctb_g2rdvar()`.
 */

#include "config.h"

#include "ctbg2rdvar_stub.h"
#include "gb2def.h"

#include <opmock.h>
#include <stdlib.h>

#ifndef __BASE_FILE__
    #define __BASE_FILE__ "BASE_FILE_REPLACEMENT" // needed by OpMock
#endif

static char* const wmoFilename0 = "g2varswmo0.tbl";;
static G2vars_t*   wmoTable0;

static char* const wmoFilename1 = "g2varswmo1.tbl";;

static char* const givenFilename = "given.tbl";;
static G2vars_t*   givenTable;

static void test_gb2gtvartbl_new_wmo_0_callback(
        char* const restrict     filename,
        G2vars_t* const restrict table,
        int* const restrict      status,
        const int                callCount)
{
    OP_ASSERT_EQUAL_CSTRING(wmoFilename0, filename);
    *status = 0;
}

void test_gb2gtvartbl_new_wmo_0()
{
    int         status;
    const char* filename;

    ctb_g2rdvar_MockWithCallback(test_gb2gtvartbl_new_wmo_0_callback);
    gb2_gtvartbl(NULL, "wmo", 0, &wmoTable0, &filename, &status);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_CSTRING(wmoFilename0, filename);
    OP_VERIFY();
}

void test_gb2gtvartbl_again_wmo_0()
{
    int         status;
    G2vars_t*   varTbl;
    char* const expectName = "g2varswmo0.tbl";;
    const char* filename;

    gb2_gtvartbl(NULL, "wmo", 0, &varTbl, &filename, &status);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_CSTRING(expectName, filename);
    OP_ASSERT_TRUE(varTbl == wmoTable0);
    OP_VERIFY();
}

static void test_gb2gtvartbl_new_wmo_1_callback(
        char* const restrict     filename,
        G2vars_t* const restrict table,
        int* const restrict      status,
        const int                callCount)
{
    OP_ASSERT_EQUAL_CSTRING(wmoFilename1, filename);
    *status = 0;
}

void test_gb2gtvartbl_new_wmo_1()
{
    int         status;
    char* const expectFilename = "g2varswmo1.tbl";;
    G2vars_t*   varTbl;
    const char* filename;

    ctb_g2rdvar_MockWithCallback(test_gb2gtvartbl_new_wmo_1_callback);
    gb2_gtvartbl(NULL, "wmo", 1, &varTbl, &filename, &status);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_CSTRING(expectFilename, filename);
    OP_ASSERT_TRUE(varTbl != wmoTable0);
    OP_VERIFY();
}

static void test_gb2gtvartbl_new_given_callback(
        char* const restrict     filename,
        G2vars_t* const restrict table,
        int* const restrict      status,
        const int                callCount)
{
    OP_ASSERT_EQUAL_CSTRING(givenFilename, filename);
    *status = 0;
}

void test_gb2gtvartbl_new_given()
{
    int         status;
    const char* filename;

    ctb_g2rdvar_MockWithCallback(test_gb2gtvartbl_new_given_callback);
    gb2_gtvartbl(givenFilename, "wmo", 0, &givenTable, &filename, &status);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_CSTRING(givenFilename, filename);
    OP_ASSERT_TRUE(givenTable != wmoTable0);
    OP_VERIFY();
}

void test_gb2gtvartbl_again_given()
{
    int         status;
    G2vars_t*   varTbl;
    const char* filename;

    gb2_gtvartbl(givenFilename, "wmo", 0, &varTbl, &filename, &status);
    OP_ASSERT_EQUAL_INT(0, status);
    OP_ASSERT_EQUAL_CSTRING(givenFilename, filename);
    OP_ASSERT_TRUE(varTbl == givenTable);
    OP_VERIFY();
}

int main(
    int		argc,
    char*	argv)
{
    opmock_test_suite_reset();
    opmock_register_test(test_gb2gtvartbl_new_wmo_0,
            "test_gb2gtvartbl_new_wmo_0");
    opmock_register_test(test_gb2gtvartbl_again_wmo_0,
            "test_gb2gtvartbl_again_wmo_0");
    opmock_register_test(test_gb2gtvartbl_new_wmo_1,
            "test_gb2gtvartbl_new_wmo_1");
    opmock_register_test(test_gb2gtvartbl_new_given,
            "test_gb2gtvartbl_new_given");
    opmock_register_test(test_gb2gtvartbl_again_given,
            "test_gb2gtvartbl_again_given");
    opmock_test_suite_run();

    return opmock_get_number_of_errors();
}

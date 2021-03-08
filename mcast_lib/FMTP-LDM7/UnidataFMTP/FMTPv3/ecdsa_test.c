/**
 * Copyright 2021 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ecdsa_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the elliptic curve digital signing algorithm (ECDSA) module
 */

#include <ecdsa.h>

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

static void test_default_signer(void)
{
    EcdsaSigner();
}

#if 0
static void test_public_key(void)
{
    EC_builtin_curve curve;

    EC_get_builtin_curves(&curve, 1);
    printf("Curve name=\"%s\"\n", curve.comment);

    EC_KEY* clntEcKey = EC_KEY_new_by_curve_name(curve.nid);
    CU_ASSERT_PTR_NOT_NULL(clntEcKey);
    CU_ASSERT_NOT_EQUAL(EC_KEY_generate_key(clntEcKey), 0);

    const EC_POINT* clntEcPoint = EC_KEY_get0_public_key(clntEcKey);
    CU_ASSERT_PTR_NOT_NULL(clntEcPoint);

    // Gets string representation of public key
    unsigned char* buf;
    size_t  n = EC_KEY_key2buf(clntEcKey, POINT_CONVERSION_COMPRESSED, &buf, NULL);
    CU_ASSERT_NOT_EQUAL(n, 0);
    printf("EC_KEY_key2buf()=%zu\n", n);
    char* hexPubKey = OPENSSL_buf2hexstr(buf, n);
    CU_ASSERT_PTR_NOT_NULL(hexPubKey);
    printf("Public key=%s\n", hexPubKey);
    OPENSSL_free(hexPubKey);

    // Decodes octet representation of public key
    EC_KEY* srvrEcKey = EC_KEY_new_by_curve_name(curve.nid);
    CU_ASSERT_PTR_NOT_NULL(srvrEcKey);
    CU_ASSERT_EQUAL(EC_KEY_oct2key(srvrEcKey, buf, n, NULL), 1);

    OPENSSL_free(buf);

    EC_KEY_free(srvrEcKey);

    // Gets string representation of public key
    const EC_GROUP* ecGroup = EC_KEY_get0_group(clntEcKey);
    CU_ASSERT_PTR_NOT_NULL(ecGroup);
    char* hexPoint = EC_POINT_point2hex(ecGroup, clntEcPoint,
            POINT_CONVERSION_COMPRESSED, NULL);
    CU_ASSERT_PTR_NOT_NULL(clntEcPoint);
    CU_ASSERT_TRUE(strlen(hexPoint) < 132);
    printf("hexPoint=\"%s\"\n", hexPoint);
    OPENSSL_free(hexPoint);

    EC_KEY_free(clntEcKey);
}

static void test_public_key(void)
{
    EC_builtin_curve curve;

    EC_get_builtin_curves(&curve, 1);
    printf("Curve name=\"%s\"\n", curve.comment);

    EC_KEY* clntEcKey = EC_KEY_new_by_curve_name(curve.nid);
    CU_ASSERT_PTR_NOT_NULL(clntEcKey);
    CU_ASSERT_NOT_EQUAL(EC_KEY_generate_key(clntEcKey), 0);

}
#endif

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_default_signer)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}

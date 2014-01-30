/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file mcast_down_ldm_test.c
 *
 * This file performs a unit-test of the mcast_down_ldm module.
 *
 * @author: Steven R. Emmerson
 */


#include "config.h"

#include "mcast_down_ldm.h"
#include "log.h"

#include "vcmtp_c_api_stub.h"
#include "pq_stub.h"

#include <errno.h>
#include <opmock.h>
#include <stdlib.h>

static void missed_product(
    void* const arg,
    signaturet signature)
{
}

void test_create_and_execute()
{
    int status;
    pqueue*     pq = (pqueue*)1;

#if 0
    do_sound_ExpectAndReturn("FIZZ", 0, cmp_cstr /*or NULL*/); /* limit 100 */
#endif
    status = mdl_create_and_execute(NULL, NULL);
    OP_ASSERT_EQUAL_LONG(EINVAL, status);
    log_clear();

    status = mdl_create_and_execute(NULL, (void*)1);
    OP_ASSERT_EQUAL_LONG(EINVAL, status);
    log_clear();

    status = mdl_create_and_execute((void*)1, NULL);
    OP_ASSERT_EQUAL_LONG(EINVAL, status);
    log_clear();

    status = mdl_create_and_execute(pq, missed_product);
    OP_ASSERT_EQUAL_LONG(0, status);

    OP_VERIFY();
}

int main(
    int		argc,
    char**	argv)
{
    opmock_test_suite_reset();
    opmock_register_test(test_create_and_execute, "test_create_and_execute");
    opmock_test_suite_run();
    return 0;
}

/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests whether closing a socket causes a read() on the socket to
 * return.
 *
 * FINDING: close() doesn't but shutdown() does
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include <arpa/inet.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr_in srvrAddr;    ///< Server socket address
static int                srvrSd = -1; ///< Server socket descriptor
static int                connSd;      ///< Server-side connection socket
static int                clntSd = -1; ///< Client socket descriptor

static int getSocket()
{
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

static int srvr_init()
{
    int status = getSocket();

    if (status != -1) {
        srvrSd = status;

        status = bind(srvrSd, (struct sockaddr*)&srvrAddr, sizeof(srvrAddr));

        if (status == 0) {
            socklen_t socklen = sizeof(srvrAddr);

            status = getsockname(srvrSd, &srvrAddr, &socklen);

            if (status == 0)
                status = listen(srvrSd, 1);
        }

        if (status) {
            (void)close(srvrSd);
            srvrSd = -1;
        }
    } // Server socket created

    return status;
}

static int srvr_destroy()
{
    return close(srvrSd);
}

static int clnt_init()
{
    int status = getSocket();

    if (status != -1) {
        clntSd = status;
        status = 0;
    } // Client socket created

    return status;
}

static int clnt_destroy()
{
    return close(clntSd);
}

/**
 * Only called once.
 */
static int setup(void)
{
    srvrAddr.sin_family = AF_INET;
    srvrAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    srvrAddr.sin_port = htons(0); // let O/S assign port

    int status = srvr_init();

    if (status == 0) {
        status = clnt_init();

        if (status)
            (void)srvr_destroy();
    } // Server socket initialized

    return status;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    clnt_destroy();
    srvr_destroy();

    return 0;
}

static void* srvr_run(void* const arg)
{
    struct sockaddr clntAddr;

    socklen_t addrLen = sizeof(clntAddr);
    connSd = accept(srvrSd, &clntAddr, &addrLen);
    CU_ASSERT_NOT_EQUAL(connSd, -1);

    char buf[1];
    int  nbytes = read(connSd, buf, sizeof(buf));
    CU_ASSERT_EQUAL(nbytes, 1);

    nbytes = read(connSd, buf, sizeof(buf));
    CU_ASSERT_EQUAL(nbytes, 0);

    (void)close(connSd);

    return NULL;
}

static int srvr_halt(void)
{
    return shutdown(connSd, SHUT_RDWR);
}

static void* clnt_run(void* const arg)
{
    int status = connect(clntSd, (struct sockaddr*)&srvrAddr, sizeof(srvrAddr));
    CU_ASSERT_EQUAL(status, 0);

    char buf[] = {0};
    int  nbytes = write(clntSd, buf, sizeof(buf));
    CU_ASSERT_EQUAL(nbytes, 1);

    nbytes = read(clntSd, buf, sizeof(buf));
    CU_ASSERT_EQUAL(nbytes, 0);

    return NULL;
}

static int clnt_halt(void)
{
    return shutdown(clntSd, SHUT_RDWR);
}

static void test_read_close(void)
{
    pthread_t srvrThread;
    int       status = pthread_create(&srvrThread, NULL, srvr_run, NULL);
    CU_ASSERT_EQUAL(status, 0);

    pthread_t clntThread;
    status = pthread_create(&clntThread, NULL, clnt_run, NULL);
    CU_ASSERT_EQUAL(status, 0);

    status = sleep(1);
    CU_ASSERT_EQUAL(status, 0);

    status = clnt_halt();
    CU_ASSERT_EQUAL(status, 0);

    status = pthread_join(clntThread, NULL);
    CU_ASSERT_EQUAL(status, 0);

    status = srvr_halt();
    CU_ASSERT_TRUE(status == -1 || status == 0);

    status = pthread_join(srvrThread, NULL);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int          argc,
        const char* const* argv)
{
    int         exitCode = 1;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_read_close)
                    ) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                if (CU_basic_run_tests() == 0)
                    exitCode = CU_get_number_of_tests_failed();
            }
        }

        CU_cleanup_registry();
    }

    return exitCode;
}

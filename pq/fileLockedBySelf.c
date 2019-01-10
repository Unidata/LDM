/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file verifies that a process can't lock a file twice without unlocking
 * it in between.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"

#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

static char pathname[] = "/tmp/fileLockedBySelfXXXXXX";
static int  fd;

/**
 * Only called once.
 */
static int setup(void)
{
    fd = mkstemp(pathname);
    if (fd == -1)
        return -1;
    return ftruncate(fd, sizeof(long));
}

/**
 * Only called once.
 */
static int teardown(void)
{
    int status = close(fd);
    if (status)
        return -1;
    status = unlink(pathname);
    if (status)
        return -1;
    return 0;
}

static void flock_init(struct flock* flock, short type)
{
    flock->l_whence = SEEK_SET; // From BOF
    flock->l_start = 0;         // BOF
    flock->l_len = 0;           // Until EOF
    flock->l_type = type;
    flock->l_pid = 0;
}

// Converting a read-lock to a write-lock is indistinguishable from simply
// applying a write-lock
static void test_canReadThenWriteLock(void)
{
    int          status;
    struct flock flock;

    flock_init(&flock, F_RDLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);

    flock_init(&flock, F_WRLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_EQUAL(status, 0);

    flock_init(&flock, F_UNLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);
}

// A process can't determine if it has a read-lock on a file
static void test_cantSeeReadLock(void)
{
    int          status;
    struct flock flock;

    flock_init(&flock, F_RDLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);

    flock_init(&flock, F_WRLCK);
    struct flock flock2 = flock;
    status = fcntl(fd, F_GETLK, &flock);
    CU_ASSERT_EQUAL(status, 0);

    // The following assertions mean that a process can't
    CU_ASSERT_EQUAL(flock.l_len, flock2.l_len);
    CU_ASSERT_EQUAL(flock.l_start, flock2.l_start);
    CU_ASSERT_EQUAL(flock.l_whence, flock2.l_whence);
    CU_ASSERT_EQUAL(flock.l_pid, flock2.l_pid);
    CU_ASSERT_EQUAL(flock.l_type, F_UNLCK);

    flock_init(&flock, F_UNLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);
}

// A process can't determine if it has a write-lock on a file
static void test_cantSeeWriteLock(void)
{
    int          status;
    struct flock flock;

    flock_init(&flock, F_WRLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);

    flock_init(&flock, F_RDLCK);
    struct flock flock2 = flock;
    status = fcntl(fd, F_GETLK, &flock);
    CU_ASSERT_EQUAL(status, 0);

    // The following assertions mean that a process can't
    CU_ASSERT_EQUAL(flock.l_len, flock2.l_len);
    CU_ASSERT_EQUAL(flock.l_start, flock2.l_start);
    CU_ASSERT_EQUAL(flock.l_whence, flock2.l_whence);
    CU_ASSERT_EQUAL(flock.l_pid, flock2.l_pid);
    CU_ASSERT_EQUAL(flock.l_type, F_UNLCK);

    flock_init(&flock, F_UNLCK);
    status = fcntl(fd, F_SETLK, &flock);
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);
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
                if (CU_ADD_TEST(testSuite, test_canReadThenWriteLock) &&
                    CU_ADD_TEST(testSuite, test_cantSeeReadLock) &&
                    CU_ADD_TEST(testSuite, test_cantSeeWriteLock)
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

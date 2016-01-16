#include "config.h"

#include <log.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>


static unsigned long            lockCount = 0;
static volatile sig_atomic_t    done = 0;


static void
printStatistics()
{
    (void)fprintf(stderr, "%ld: Lock count: %lu\n", (long)getpid(), lockCount);
}


static int
createTestFile(
    const char* const   pathname,       /* pathname of file */
    off_t               size)           /* size of file */
{
    int success = 0;

    log_assert(pathname != NULL);
    log_assert(pathname[0] != 0);
    log_assert(size >= 0);

    if (unlink(pathname) == -1 && errno != ENOENT) {
        (void)fprintf(stderr, "Couldn't delete file \"%s\": %s\n", pathname,
            strerror(errno));
    }
    else {
        int     fd = creat(pathname, S_IRWXU | S_IRWXG | S_IRWXO);

        if (fd == -1) {
            (void)fprintf(stderr, "Couldn't create file \"%s\": %s\n", pathname,
                strerror(errno));
        }
        else {
            int         i;
            char        c[1] = {0};

            for (i = 0; i < size; i++) {
                if (write(fd, c, 1) != 1) {
                    (void)fprintf(stderr,
                        "Couldn't clear %ld bytes in file \"%s\": %s\n",
                        (long)size, pathname, strerror(errno));
                    break;
                }
            }
            if (i >= size)
                success = 1;

            (void)close(fd);
        }
    }

    return success;
}


static void
randomSleep(
    useconds_t  maxInterval)            /* maximum sleep interval */
{
    int                 success = 1;
    static int          initialized = 0;
    useconds_t          interval;
    int                 seconds;

    log_assert(maxInterval >= 0);

    if (!initialized) {
        srand((unsigned)getpid());
        initialized = 1;
    }
    
    interval = (useconds_t)(maxInterval * (rand() / (double)RAND_MAX));
    seconds = interval / 1000000;

    if (!done && seconds > 0) {
        if (sleep(seconds) != 0) {
            (void)fprintf(stderr, "%ld: Couldn't sleep %d seconds: %s\n",
                (long)getpid(), seconds, strerror(errno));
            success = 0;
        }
        interval %= 1000000;
        log_assert(interval >= 0);
    }

    if (!done && success) {
        if (usleep(interval) == -1) {
            (void)fprintf(stderr, "%lu: Couldn't sleep %ld microseconds: %s\n",
                (long)getpid(), (unsigned long)interval, strerror(errno));
        }
    }
}


int
lockFile(
    int         fd,                     /* file-descriptor of file */
    size_t      len)                    /* amount of file to lock */
{
    int                 success = 0;
    struct flock        lock;           /* locking structure */

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = len;

    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        (void)fprintf(stderr, "%ld: Couldn't lock file: %s\n", 
            (long)getpid(), strerror(errno));
    }
    else {
        success = 1;
    }

    return success;
}


int
unlockFile(
    int         fd,                     /* file-descriptor of file */
    size_t      len)                    /* amount of file to unlock */
{
    int                 success = 0;
    struct flock        lock;           /* locking structure */

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = len;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        (void)fprintf(stderr, "%ld: Couldn't unlock file: %s\n", 
            (long)getpid(), strerror(errno));
    }
    else {
        success = 1;
    }

    return success;
}


static void
testLocking(
    const char* const   pathname,       /* pathname of file */
    const size_t        len,            /* length of memory-mapped portion */
    useconds_t          maxInterval)    /* maximum sleep interval */
{
    const int   fd = open(pathname, O_RDWR, 0);

    log_assert(len >= 0);

    if (fd == -1) {
        (void)fprintf(stderr, "%ld: Couldn't open file \"%s\": %s\n",
            (long)getpid(), pathname, strerror(errno));
    }
    else {
        /*
         * Memory-map the file.
         */
        void*   addr = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

        if (addr == MAP_FAILED) {
            (void)fprintf(stderr, "%ld: Couldn't memory-map file \"%s\": %s\n",
                (long)getpid(), pathname, strerror(errno));
        }
        else {
            log_assert(addr != NULL);

            while (!done) {
                randomSleep(maxInterval);

                /*
                 * Lock the file.
                 */
                if (!lockFile(fd, len))
                    break;
                lockCount++;

                /*
                 * Change the file.
                 */
                (void)memset(addr, 1+*(char*)addr, len);

                randomSleep(maxInterval);

                /*
                 * Unlock the file.
                 */
                if (!unlockFile(fd, len))
                    break;
            }                           /* while not done */

            (void)munmap(addr, len);
        }                               /* "addr" is mapped */

        (void)close(fd);
    }                                   /* "fd" is open */
}


static void
sigHandler(
    int sig)
{
    done = 1;
}


static pid_t
startChildLocker(
    const char* const   pathname,       /* pathname of file */
    const size_t        len,            /* size of file */
    useconds_t          maxInterval)    /* maximum sleep interval */
{
    int pid = fork();

    if (pid == -1) {
        perror("Couldn't fork child process");
    }
    else if (pid == 0) {
        /*
         * Child process.
         */
        (void)fprintf(stderr, "%ld: Started\n", (long)getpid());
        if (signal(SIGHUP, sigHandler) == SIG_ERR ||
                signal(SIGTERM, sigHandler) == SIG_ERR ||
                signal(SIGINT, sigHandler) == SIG_ERR) {
            (void)fprintf(stderr, "%ld: signal() failure: %s\n",
                (long)getpid(), strerror(errno));
            _exit(EXIT_FAILURE);
        }
        testLocking(pathname, len, maxInterval);
        printStatistics();
        exit(done ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    return pid;
}


int
main(
    int         argc,
    char*       argv[])
{
#   define DEFAULT_PATHNAME "lockTest.test"
    const char* pathname = DEFAULT_PATHNAME;
    off_t       size = 8192;
    int         status = EXIT_SUCCESS;
    int         c;
#   define DEFAULT_MAX_INTERVAL 50
    useconds_t  maxInterval = DEFAULT_MAX_INTERVAL;

    while ((c = getopt(argc, argv, "f:i:")) != -1) {
        switch(c) {
        case 'f': {
            pathname = optarg;
            break;
        }
        case 'i': {
            unsigned long       max;
            int                 nbytes;
            if (sscanf(optarg, "%lu %n", &max, &nbytes) != 1 ||
                    optarg[nbytes] != 0 || max < 0) {
                (void)fprintf(stderr,
                    "Invalid maximum sleep interval \"%s\"\n", optarg);
                status = EXIT_FAILURE;
            }
            maxInterval = (useconds_t)max;
            break;
        }
        case '?':
            (void)fprintf(stderr, "Unrecognized option \"%c\"\n", optopt);
            status = EXIT_FAILURE;
            break;
        }
    }
    if (optind < argc) {
        (void)fprintf(stderr, "Unrecognized argument \"%s\"\n", argv[optind]);
        status = EXIT_FAILURE;
    }

    if (status == EXIT_FAILURE) {
        (void)fprintf(stderr,
"Usage: %s [-f pathname] [-i maxInterval]\n"
"  where:\n"
"    -f pathname     Pathname of test file (default: \"./%s\").\n"
"    -i maxInterval  Maximum sleep interval between fcntl()\n"
"                    calls in integral microseconds (default: %d).\n",
            argv[0], DEFAULT_PATHNAME, DEFAULT_MAX_INTERVAL);
    }
    else {
        status = EXIT_FAILURE;

        if (createTestFile(pathname, size)) {
            pid_t pid = startChildLocker(pathname, size, maxInterval);

            if (pid != -1) {
                pid = startChildLocker(pathname, size, maxInterval);

                if (pid != -1) {
                    pause();
                    status = EXIT_SUCCESS;
                }
            }
        }
    }

    exit(status);
}

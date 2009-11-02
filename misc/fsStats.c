/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include "ldmconfig.h"

#include <stddef.h>
#include <sys/types.h>

#include "fsStats.h"

#if HAVE_FSTATVFS
#    include <sys/statvfs.h>
#elif HAVE_FSTATFS
#    include <sys/vfs.h>
#endif

/*
 * Returns the size of a disk partition and the amount of free space.
 * ARGUMENTS:
 *      fd      File descritor of an open file in the partition
 *      total   Returned partition size in bytes
 *      avail   Returned free space for non-superusers in bytes
 * RETURNS:
 *      0       Success
 *      else    <errno.h> error code.
 */
int
fsStats(
    int         fd,
    off_t*      total,
    off_t*      avail)
{
    int                 status;
#if HAVE_FSTATVFS
    struct statvfs      buf;
    if (fstatvfs(fd, &buf) == -1) {
        status = errno;
    }
#elif HAVE_FSTATFS
    struct statfs       buf;
    if (fstatfs(fd, &buf) == -1) {
        status = errno;
    }
#else
    status = ENOSYS;
#endif
    else {
        off_t           blockSize =
            buf.f_frsize > 0 ? buf.f_frsize : buf.f_bsize;
        if (total != NULL)
            *total = blockSize * buf.f_blocks;
        if (avail != NULL)
            *avail = blockSize * buf.f_bavail;
        status = 0;
    }
    return status;
}


#ifdef TEST_FSSTATS /* test driver */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


static void
usage(const char *const av0)
{
        (void)fprintf(stderr, "Usage: %s filename\n",
                av0);
        exit(EXIT_FAILURE);
}


main(int ac, char *av[])
{
        int status = ENOERR;
        int fd;
        const char *const path = av[ac-1];
        off_t total;
        off_t avail;

        if(ac != 2)
                usage(av[0]);

        fd = open(path, O_RDONLY, 0);
        if(fd == -1)
        {
                status = errno;
                (void)fprintf(stderr, "open %s failed: %s\n",
                        path, strerror(status));
                exit(EXIT_FAILURE);
        }

        status = fsStats(fd, &total, &avail);
        if(status != ENOERR)
        {
                (void)fprintf(stderr, "fsStats %s failed: %s\n",
                        path, strerror(status));
                exit(EXIT_FAILURE);
        }

        printf("File system size: %10ld (%7ld k)\n",
                (long)total, (long)total/1024);
        printf(" space available: %10ld (%7ld k)\n",
                (long)avail, (long)avail/1024);

        exit(EXIT_SUCCESS);
}
#endif /* TEST_FSSTATS */

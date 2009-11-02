#ifndef _FSSTATS_H_
#define _FSSTATS_H_
/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include <sys/types.h> /* off_t */
#include <errno.h>
#ifndef ENOERR
#define ENOERR 0
#endif

/*
 * Wrapper around fstatvfs or fstatfs.
 *
 * On failure, return errno value indicating cause of
 * failure.
 *
 * On success, returns ENOERR and sets '*fs_szp' to
 * size in bytes of the unix partition
 * where the file corresponding to 'fd' resides,
 * And sets '*remp' to the number of bytes available to
 * a non-super-user on the partition.
 */
int
fsStats(int fd, off_t *fs_szp, off_t *remp);

#endif /*!_FSSTATS_H_*/

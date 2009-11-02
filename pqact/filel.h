/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */
#ifndef _FILEL_H_
#define _FILEL_H_

#include <sys/types.h> /* pid_t */
#include "ldm.h"

/*
 * fl_entry.flags, args to close_lru()
 */
#define FL_NEEDS_SYNC 1
#define FL_OVERWRITE 2
#define FL_NOTRANSIENT 16
#define FL_STRIP 32
#define FL_LOG 64
#define FL_METADATA 128	/* write data-product metadata */
#define FL_NODATA 256 /* don't write data */

#ifdef __cplusplus
extern "C" {
#endif

extern int unio_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
extern int stdio_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
extern int pipe_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
extern int spipe_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
extern int xpipe_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
#ifndef NO_DB
extern int ldmdb_prodput( const product *prod, int argc, char **argv,
	const void *xprod, size_t xlen);
#endif /* !NO_DB */
extern pid_t reap(
    const pid_t pid,
    const int   options);
extern void fl_sync(int nentries, int block);
extern void close_lru(int skipflags);
extern void fl_close_all(void);
extern void endpriv(void);
extern int set_avail_fd_count(unsigned fdCount);
extern long openMax();

#ifdef __cplusplus
}
#endif

#endif /* !_FILEL_H_ */

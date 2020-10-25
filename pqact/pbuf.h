/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#ifndef _PBUF_H_
#define _PBUF_H_

#include <stddef.h>
#include <errno.h>
#ifndef ENOERR
#define ENOERR 0
#endif /*!ENOERR */

typedef struct {
	int pfd;
	char *base; /* actual storage */
	char *ptr; /* current position */
	char *upperbound; /* base + bufsize */
} pbuf;


#ifdef __cplusplus
extern "C" void free_pbuf(pbuf *buf);
extern "C" pbuf * new_pbuf(int pfd, size_t bufsize);
extern "C" int pbuf_flush(
    pbuf*               buf,
    int                 block,          /* bool_t */
    unsigned int        timeo);         /* N.B. Not a struct timeval */
extern "C" int pbuf_write(
    pbuf*               buf,
    const char*         ptr,
    size_t              nbytes,
    unsigned int        timeo);         /* N.B. Not a struct timeval */
#elif defined(__STDC__)
extern void free_pbuf(pbuf *buf);
extern pbuf * new_pbuf(int pfd, size_t bufsize);
extern int pbuf_flush(
    pbuf*               buf,
    int                 block,          /* bool_t */
    unsigned int        timeo,          /* N.B. Not a struct timeval */
    const char*         cmd);           /* Command on other side of pipe */
extern int pbuf_write(
    pbuf*               buf,
    const char*         ptr,
    size_t              nbytes,
    unsigned int        timeo,          /* N.B. Not a struct timeval */
    const char*         cmd);           /* Command on other side of pipe */
#else /* Old Style C */
extern void free_pbuf();
extern pbuf * new_pbuf();
extern int pbuf_flush();
extern int pbuf_write();
#endif

#endif /* !_PBUF_H_ */

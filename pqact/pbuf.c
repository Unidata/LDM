/*
 *   Copyright 2012, University Corporation for Atmospheric Research
 *   All rights reserved.
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include <config.h>
#include "pbuf.h"
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "ulog.h"
#include "mylog.h"
#include "ldmalloc.h"
#include "alrm.h"
#include "error.h"
#include "fdnb.h"

#ifndef HAVE_MEMMOVE
    /* define memmove in terms of bcopy - recividist */
    #define memmove(d1, d2, n) bcopy((d2), (d1), (n))
#endif

void
free_pbuf(pbuf *buf)
{
        if(buf == NULL)
                return;
        free(buf->base);
        free(buf);
}

pbuf *
new_pbuf(
        int pfd,
        size_t bufsize)
{
        long pipe_buf = 512; /* _POSIX_PIPE_BUF */
        pbuf *buf = NULL;
        buf = Alloc(1, pbuf);
        if(buf == NULL)
                return NULL;
#ifdef _PC_PIPE_BUF
        pipe_buf = fpathconf(pfd, _PC_PIPE_BUF);
        if(pipe_buf == -1L)
        {
                serror("fpathconf %d, _PC_PIPE_BUF", pfd);
                goto err;
        }
#endif
        set_fd_nonblock(pfd);
        buf->pfd = pfd;
        if(bufsize < (size_t)pipe_buf)
                bufsize = (size_t)pipe_buf;
        buf->base = Alloc(bufsize, char);
        if(buf->base == NULL)
                goto err;
        buf->ptr = buf->base;
        buf->upperbound = buf->base + bufsize;
        return buf;
err:
        free(buf);
        return NULL;
}

/**
 * Flushes data to the pipe.
 *
 * @param[in] buf        Pipe buffer.
 * @param[in] block      Whether or not write should block.
 * @param[in] timeo      Timeout in seconds. 0 means indefinite timeout.
 * @retval    0          Success.
 * @retval    EAGAIN     `block` is false and write would block.
 * @retval    EINTR      Write to pipe was interrupted by signal.
 * @retval    EPIPE      Pipe not open for reading. Reader likely terminated.
 * @retval    ETIMEDOUT  Write to pipe timed-out.
 */
int
pbuf_flush(
    pbuf*               buf,
    int                 block,          /* bool_t */
    unsigned int        timeo)          /* N.B. Not a struct timeval */
{
    size_t              len = (size_t)(buf->ptr - buf->base);
    int                 changed = 0;
    int                 nwrote = 0;
    int                 status = ENOERR;        /* success */
    int                 tmpErrno;
    time_t              start;
    time_t              duration;

    udebug("        pbuf_flush fd %d %6d %s",
        buf->pfd, len, block ? "block" : "" );

    if(len == 0)
        return 0; /* nothing to do */
    /* else */

    (void)time(&start);

    if(block)
        changed = clr_fd_nonblock(buf->pfd);

    if(block && timeo != 0)             /* (timeo == 0) => don't set alarm */
        SET_ALARM(timeo, flush_timeo);

    nwrote = (int) write(buf->pfd, buf->base, len);
    tmpErrno = errno;                   /* CLR_ALRM() can change "errno" */

    if(block && timeo != 0)
        CLR_ALRM();

    if(nwrote == -1) {
        if((tmpErrno == EAGAIN) && (!block)) {
            udebug("         pbuf_flush: EAGAIN on %d bytes", len);
            nwrote = 0;
        }
        else {
            serror("pbuf_flush(): fd=%d", buf->pfd);
        }
        status = tmpErrno;
    }
    else if(nwrote == len) {
        /* wrote the whole buffer */
        udebug("         pbuf_flush: wrote  %d bytes", nwrote);

        buf->ptr = buf->base;
        len = 0;
    }
    else if(nwrote > 0) {
        /* partial write, just shift the buffer by the amount written */
        udebug("         pbuf_flush: partial write %d of %d bytes",
            nwrote, len);

        len -= nwrote;

        /* could be an overlapping copy */
        memmove(buf->base, buf->base + nwrote, len);

        buf->ptr = buf->base +len;
    }

    if(changed)
        set_fd_nonblock(buf->pfd);

    duration = time(NULL) - start;

    if(duration > 5)
        uwarn("pbuf_flush(): write(%d,,%d) to decoder took %lu s",
            buf->pfd, nwrote, (unsigned long)duration);

    return status;

flush_timeo:
    if(changed)
        set_fd_nonblock(buf->pfd);

    uerror("pbuf_flush(): write(%d,,%lu) to decoder timed-out (%lu s)",
        buf->pfd, (unsigned long)len, (unsigned long)(time(NULL) - start));

    return ETIMEDOUT;
}

/**
 * Writes to a pipe-buffer.
 *
 * @param[in] buf        Pipe buffer.
 * @param[in] ptr        Data to write.
 * @param[in] nbytes     Number of bytes to write.
 * @param[in] timeo      Timeout in seconds. 0 means indefinite timeout.
 * @retval    0          Success.
 * @retval    EINTR      Write to pipe was interrupted by signal.
 * @retval    EPIPE      Pipe not open for reading. Reader likely terminated.
 * @retval    ETIMEDOUT  Write to pipe timed-out.
 */
int
pbuf_write(
    pbuf*               buf,
    const char*         ptr,
    size_t              nbytes,
    unsigned int        timeo)          /* N.B. Not a struct timeval */
{
    int    status;
    size_t tlen;

    while (nbytes > 0) {
        tlen = (size_t)(buf->upperbound - buf->ptr);
        tlen = (nbytes < tlen) ? nbytes : tlen;

        memcpy(buf->ptr, ptr, tlen);

        buf->ptr += tlen;

        if(buf->ptr == buf->upperbound) {
            status = pbuf_flush(buf, 1, timeo);

            if(status != ENOERR)
                return status;
        }

        ptr += tlen;
        nbytes -= tlen;
    }

    /* write what we can */
    status = pbuf_flush(buf, 0, 0);

    return (EAGAIN == status) ? 0 : status;     // OK if write would block
}

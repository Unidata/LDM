/*
 *   Copyright 2012, University Corporation for Atmospheric Research
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

#include <config.h>
#include <string.h>
#include "xbuf.h"
#ifndef MCIDAS_ONLY
#include "feed.h"
#endif
#include "mylog.h"
#ifndef NULL
#define NULL 0
#endif

/*
 * application specific xbuf code
 */

#ifndef INIT_CIRCBUFSIZE
#define INIT_CIRCBUFSIZE 16384
#endif

#ifndef CHUNKSIZE
#define CHUNKSIZE 4096
#endif

/* allocated by initTheXbuf() */
static xbuf *theBuf  = NULL;
/* pointer set to the right function by initTheXbuf() */
static int (*read_feed)(int ifd, char *, size_t, size_t *);

/* pointer set to the right function by setTheScanner() */
static int (*theScanner)(xbuf *buf) = NULL;

/* The size, in bytes, of the largest expected data-product */
static unsigned long maxProductSize;

void
setTheScanner(int (*scanner)(xbuf *buf))
{
	theScanner = scanner;	
}

/**
 * Initializes this module.
 * 
 * @param readfunct     [in] The function that reads the data
 * @param maxProdSize   [in] The size, in bytes, of the largest expected 
 *                      data-product
 * @retval 0            Success
 * @retval ENOMEM       Out of memory
 */
int
initTheXbuf(
        int (*readfunct)(int ifd, char *buf, size_t nbytes, size_t *ngotp),
        const unsigned long maxProdSize)
{
	read_feed = readfunct;
    maxProductSize = maxProdSize > INIT_CIRCBUFSIZE ? maxProdSize : INIT_CIRCBUFSIZE;

	if(theBuf == NULL)
	{
		theBuf = new_xbuf(INIT_CIRCBUFSIZE);
		if(theBuf == NULL)
		{
			const int status = errno == 0 ? ENOMEM : errno;
			mylog_syserr("new_xbuf");
			return status;
		}
	}
	return ENOERR;
}


/**
 * There is data available on the feed. Read it into the buffer
 * then deal with what we got.
 *
 * @param ifd           [in] File-descriptor of the input data-feed
 * @retval 0            Success
 * @retval ENOMEM       Out of memory
 * @retval ENODATA      End of input data-feed
 */
int
feedTheXbuf(const int ifd)
{
	int status;
	size_t nn = 0;
	/* space available in buffer */
	ptrdiff_t remaining = (ptrdiff_t)theBuf->bufsiz - (theBuf->get - theBuf->base);

	if (remaining <= CHUNKSIZE) {
		if (theBuf->bufsiz >= maxProductSize) {
			mylog_warning(
			        "Data-product would exceed %lu bytes. Resetting input buffer.",
				maxProductSize);
			justify_xbuf(theBuf, 0);
		}

		mylog_info("Expanding input buffer size to %lu\n",
			(unsigned long)(2 * theBuf->bufsiz));

		theBuf = expand_xbuf(theBuf, theBuf->bufsiz);

		if (theBuf == NULL) {
			status = errno == 0 ? ENOMEM : errno;
			mylog_syserr("expand_xbuf");
			return status;
		}
	}

	status = (*read_feed)(ifd, (char *)theBuf->put, CHUNKSIZE, &nn);
	if(status != ENOERR)
	{
		mylog_errno(status, "read_feed");
		return status;
	}
	/* else */
	if(nn == 0)
		return ENODATA; /* end of file */
	/* else */
	/* usual case */
	/* assert(nn > 0); */
	theBuf->cnt += nn;
	theBuf->put += nn;
	return ENOERR;
}


int
scanTheXbuf(void)
{
	return (*theScanner)(theBuf);
}

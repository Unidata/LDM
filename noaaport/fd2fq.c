/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: fd2fq.c
 * @author: Steven R. Emmerson
 *
 * This file conveys NBS frames from a file-descriptor to a frame-queue.
 */

#include "config.h"

#include "frame_queue.h"
#include "log.h"
#include "fd2fq.h"

#include <stdint.h>
#include <unistd.h>

/******************************************************************************
 * Private:
 ******************************************************************************/

// File-descriptor-to-frame-queue structure:
struct fd2fq {
    fq_t* fq; ///< Frame queue
    int   fd; ///< File descriptor
};

/**
 * Initializes a file-descriptor-to-frame-queue object.
 *
 * @param[out] fd2fq          File-descriptor-to-frame-queue object to be
 *                            initialized.
 * @param[in]  fd             File-descriptor from which to read NBS frames
 * @param[in]  fq             Frame queue
 * @retval 0                  Success. `*fd2fq` is set.
 * @retval NBS_STATUS_INVAL   `fd2fq == NULL || fd < 0 || fq == NULL`. log_add()
 *                            called.
 * @retval NBS_STATUS_NOMEM   Out of memory. log_add() called.
 */
static nbs_status_t fd2fq_init(
        fd2fq_t* const restrict fd2fq,
        const int               fd,
        fq_t* const restrict    fq)
{
    int status;
    if (fd2fq == NULL || fd < 0 || fq == NULL) {
        log_add("Invalid argument: fd2fq=%p, fd=%d, fq=%p", fd2fq, fd, fq);
        status = NBS_STATUS_INVAL;
    }
    else {
        fd2fq->fd = fd;
        fd2fq->fq = fq;
        status = 0;
    }
    return status;
}

/**
 * Transfers an NBS frame from the file-descriptor to the frame-queue.
 *
 * @param[in] fd2fq           File-descriptor-to-frame-queue object
 * @retval 0                  Success
 * @retval NBS_STATUS_END     Input is shut down
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_INVAL   Frame queue can't handle NBS frame size. log_add()
 *                            called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t fd2fq_transfer_frame(
        fd2fq_t* const fd2fq)
{
    uint8_t* data;
    unsigned nbytes = 65507; // Max UDP payload
    int      status = fq_reserve(fd2fq->fq, &data, nbytes);
    if (status) {
        log_add("Can't reserve %u bytes of space", nbytes);
        status = (status == FQ_STATUS_INVAL)
                ? NBS_STATUS_LOGIC
                : (status == FQ_STATUS_TOO_BIG)
                  ? NBS_STATUS_INVAL
                  : NBS_STATUS_SYSTEM;
    }
    else {
        nbytes = read(fd2fq->fd, data, nbytes);
        if (nbytes < 0) {
            log_syserr("Couldn't read from file-descriptor %d", fd2fq->fd);
            status = NBS_STATUS_SYSTEM;
        }
        else if (nbytes == 0) {
            status = NBS_STATUS_END;
        }
        else {
            status = fq_release(fd2fq->fq, nbytes);
            if (status)
                status = NBS_STATUS_LOGIC;
        }
    }
    return status;
}

/**
 * Finalizes a file-descriptor-to-frame-queue object.
 *
 * @param[in] fd2fq  File-descriptor-to-frame-queue object to be finalized or
 *                   `NULL`.
 */
static void fd2fq_fini(
        fd2fq_t* const fd2fq)
{
}

/******************************************************************************
 * Public:
 ******************************************************************************/

/**
 * Transfers NBS frames from a file-descriptor to a frame-queue. Doesn't return
 * unless the input or output is shut down or an unrecoverable error occurs.
 *
 * @param[in]  fd              File-descriptor from which to read NBS frames
 * @param[in]  fq              Frame queue
 * @retval 0                   Input was shut down
 * @retval NBS_STATUS_LOGIC    Logic error. log_add() called.
 * @retval NBS_STATUS_INVAL   `fd2fq == NULL` or frame queue can't handle NBS
 *                             frame size. log_add() called.
 * @retval NBS_STATUS_SYSTEM   System failure. log_add() called.
 */
nbs_status_t fd2fq_execute(
        const int            fd,
        fq_t* const restrict fq)
{
    fd2fq_t fd2fq;
    int     status = fd2fq_init(&fd2fq, fd, fq);
    while (status == 0)
        status = fd2fq_transfer_frame(&fd2fq);
    fd2fq_fini(&fd2fq);
    return (status == NBS_STATUS_END) ? 0 : status;
}

/**
 * This file declares an API for reading NOAAPort Broadcast System (NBS) frames.
 *
 *        File: NbsFrame.h
 *  Created on: Jan 31, 2022
 *      Author: Steven R. Emmerson
 */

#ifndef NOAAPORT_NBSFRAME_H_
#define NOAAPORT_NBSFRAME_H_

#include "log.h"
#include "NbsHeaders.h"

#include <stddef.h>
#include <stdint.h>

#define NBS_FH_SIZE  16    ///< Canonical frame header size in bytes
#define NBS_PDH_SIZE 16    ///< Canonical product-definition header size in bytes

/// NBS return codes:
enum {
    NBS_SUCCESS, ///< Success
    NBS_SPACE,   ///< Insufficient space for frame
    NBS_EOF,     ///< End-of-file read
    NBS_IO,      ///< I/O error
    NBS_INVAL    ///< Invalid frame
};

typedef struct NbsReader {
    NbsFH    fh;                               ///< Decoded frame header
    NbsPDH   pdh;                              ///< Decoded product-definition header
    uint8_t* end;                              ///< One byte beyond buffer contents
    uint8_t* nextFH;                           ///< Start of next frame header in buffer
    size_t   size;                             ///< Active frame size in bytes
    int      fd;                               ///< Input file descriptor
    bool     logSync;                          ///< Log synchronizing message?
    uint8_t  buf[NBS_MAX_FRAME + NBS_FH_SIZE]; ///< Frame buffer
} NbsReader;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getBytes(int fd, uint8_t* buf, size_t nbytes);

/**
 * Initializes a NBS frame reader.
 *
 * @param[out] reader  NBS frame reader
 * @param[in]  fd      Input file descriptor. Will be closed by `nbs_destroy()`.
 */
void nbs_init(
        NbsReader* reader,
        const int  fd);

/**
 * Destroys a NBS frame reader.
 *
 * @param[in] reader  NBS frame reader
 */
void nbs_destroy(NbsReader* reader);

/**
 * Returns a new NBS frame reader.
 *
 * @param[in] fd  Input file descriptor. Will be closed by `nbs_deleteReader()`.
 * @return        New reader
 * @retval NULL   System failure. `log_add()` called.
 * @see `nbs_freeReader()`
 */
NbsReader* nbs_newReader(int fd);

/**
 * Frees the resources associated with an NBS frame reader. Closes the
 * file descriptor given to `nbs_newReader()`.
 *
 * @param[in] reader  NBS reader
 * @see `nbs_newReader()`
 */
void nbs_freeReader(NbsReader* reader);

/**
 * Returns the next NBS frame.
 *
 * @param[in]  reader   NBS reader
 * @retval NBS_SUCCESS  Success
 * @retval NBS_SPACE    Input frame is too large for buffer
 * @retval NBS_EOF      End-of-file read
 * @retval NBS_IO       I/O failure
 */
int nbs_getFrame(NbsReader* const reader);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_NBSFRAME_H_ */

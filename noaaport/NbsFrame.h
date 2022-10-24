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
#define NBS_TCH_SIZE 32    ///< Canonical time-command header size in bytes

/// NBS return codes:
enum {
    NBS_SUCCESS, ///< Success
    NBS_SPACE,   ///< Insufficient space for frame
    NBS_EOF,     ///< End-of-file read
    NBS_IO,      ///< I/O error
    NBS_INVAL,   ///< Invalid frame
    NBS_SYSTEM   ///< System failure
};

typedef struct NbsReader NbsReader;

#ifdef __cplusplus
extern "C" {
#endif

//ssize_t getBytes(int fd, uint8_t* buf, size_t nbytes);

/**
 * Returns an NBS frame reader.
 *
 * @param[in]  fd      Input file descriptor. Will be closed by `nbs_destroy()`.
 * @retval     NULL    Allocation failure. `log_add()` called.
 * @return             Pointer to new instance
 * @see                `nbs_free()`
 */
NbsReader* nbs_new(const int fd);

/**
 * Frees an NBS frame reader.
 *
 * @param[in] reader  NBS frame reader
 * @see               `nbs_new()`
 */
void nbs_free(NbsReader* reader);

/**
 * Returns the next NBS frame.
 *
 * @param[in]  reader   NBS reader
 * @param[out] frame    NBS frame
 * @param[out] size     Size of NBS frame in bytes
 * @param[out] fh       NBS frame header
 * @param[out] pdh      NBS product-definition header or NULL. Set iff `fh->command ==
 *                      NBS_FH_CMD_DATA`.
 * @retval NBS_SUCCESS  Success. `*frame`, `*size`, and `*fh` are set.
 * @retval NBS_EOF      End-of-file read. `log_add()` called.
 * @retval NBS_IO       I/O failure. `log_add()` called.
 */
int nbs_getFrame(
        NbsReader* const reader,
        uint8_t** const  frame,
        size_t* const    size,
        NbsFH** const    fh,
        NbsPDH** const   pdh);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_NBSFRAME_H_ */

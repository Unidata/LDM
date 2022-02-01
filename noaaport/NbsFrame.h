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

enum {
    NBS_SUCCESS, ///< Success
    NBS_SPACE,   ///< Insufficient space for frame
    NBS_EOF,     ///< End-of-file read
    NBS_IO,      ///< I/O error
    NBS_INVAL    ///< Invalid frame
};

typedef struct NbsReader  NbsReader;
typedef NbsFrameHeader    NbsFH;
typedef NbsProdDefHeader  NbsPDH;
typedef NbsProdSpecHeader NbsPSH;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getBytes(int fd, uint8_t* buf, size_t nbytes);

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
 * @param[in]  reader  NBS reader
 * @param[out] buf     Pointer to NBS frame. May be NULL.
 * @param[out] size    Size of NBS frame. May be NULL.
 * @param[out] fh      Pointer to decoded frame header. May be NULL.
 * @param[out] pdh     Pointer to decoded product-definition header. May be
 *                     NULL.
 * @param[out] psh     Pointer to decoded product-specific header. May be NULL.
 * @retval NBS_SUCCESS  Success. `buf`, `size`, `fh`, `pdh`, and `psh` are set
 *                      if non-NULL.
 * @retval NBS_EOF  End-of-file read
 * @retval NBS_IO   I/O failure
 */
int nbs_getFrame(
        NbsReader* const      reader,
        const uint8_t** const buf,
        size_t*               size,
        const NbsFH** const   fh,
        const NbsPDH** const  pdh,
        const NbsPSH** const  psh);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_NBSFRAME_H_ */

/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */

#ifndef GRIB2_DECODER_H
#define GRIB2_DECODER_H

#include "grib2.h"

#include <stddef.h>

typedef struct DecodedGrib2Msg  DecodedGrib2Msg;
typedef struct Grib2Field       Grib2Field;
typedef struct Grib2Section     Grib2Section;

/**
 * Return codes:
 */
enum {
    G2D_SUCCESS = 0,
    G2D_INVALID,
    G2D_NOT_2,
    G2D_END,
    G2D_SYSERR
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a GRIB-2 section's parameter as an integer.
 *
 * @param[in]  section      Pointer to the GRIB-2 section.
 * @param[in]  iByte        Offset to the start of the parameter from the start
 *                          of the section in bytes.
 * @param[in]  nBytes       Number of bytes in the parameter.
 * @param[out] value        Pointer to the integer in which to put the value.
 * @retval     0            Success. \c *value is set.
 * @retval     G2D_INVALID  The byte-sequence specification is invalid.
 */
int g2s_getG2Int(
    const Grib2Section* const section,
    const size_t              iByte,
    const unsigned            nBytes,
    g2int* const              value);

/**
 * Returns the GRIB-2 section of a GRIB-2 field corresponding to a given index.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  field        Pointer to decoded GRIB-2 field.
 * @param[in]  index        GRIB-2 index of the field's section to return (1,
 *                          3-7).
 * @param[out] section      Address of pointer to section.
 * @retval     0            Success. \c *section is set.
 * @retval     G2D_INVALID  Invalid index.
 */
int g2f_getSection(
    const Grib2Field* const   field,
    const unsigned            index,
    Grib2Section** const      section);

/**
 * Returns a decoded GRIB-2 message corresponding to an encoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[out] decoded          Address of pointer to decoded GRIB-2 message.
 *                              The client should call \c g2d_free(*decoded)
 *                              when the decoded GRIB-2 message is no longer
 *                              needed.
 * @param[in]  buf              Pointer to the start of an encoded GRIB-2
 *                              message. The message must start with the
 *                              character sequence "GRIB". The client must not
 *                              alter or free the message until the client calls
 *                              \c g2d_free().
 * @param[in]  bufLen           The length of the message in bytes.
 * @retval     0                Success. \c *decoded is set.
 * @retval     G2D_INVALID      Invalid message.
 * @retval     G2D_NOT_2        Not GRIB edition 2.
 * @retval     G2D_SYSERR       System error.
 */
int g2d_new(
    DecodedGrib2Msg** const decoded,
    const unsigned char*    buf,
    size_t                  bufLen);

/**
 * Frees a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded  Pointer to decoded GRIB-2 message or NULL.
 */
void g2d_free(
    DecodedGrib2Msg* const decoded);

/**
 * Returns the associated encoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Pointer to encoded GRIB-2 message.
 */
const unsigned char* g2d_getBuf(
    DecodedGrib2Msg* decoded);

/**
 * Returns the length, in bytes, of the associated encoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Length of the associated encoded GRIB-2 message in bytes.
 */
size_t g2d_getBufLen(
    DecodedGrib2Msg* decoded);

/**
 * Returns the section 1 of a decoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Pointer to section 1 of the decoded GRIB-2 message.
 */
const Grib2Section* g2d_getSection1(
    DecodedGrib2Msg* decoded);

/**
 * Returns the originating center.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded            Pointer to decoded GRIB-2 message.
 * @return                        The ID of the originating center.
 */
g2int g2d_getOriginatingCenter(
    DecodedGrib2Msg* const decoded);

/**
 * Returns the originating sub-center.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded            Pointer to decoded GRIB-2 message.
 * @return                        The ID of the originating sub-center.
 */
g2int g2d_getOriginatingSubCenter(
    DecodedGrib2Msg* const decoded);

/**
 * Returns the number of fields in a decoded GRIB-2 message.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in] decoded  Pointer to the decoded GRIB-2 message
 * @return             The number of fields in the decoded GRIB-2 message.
 */
size_t g2d_getNumFields(
    const DecodedGrib2Msg* const decoded);

/**
 * Returns the GRIB-2 field corresponding to a given index.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded      Pointer to the decoded GRIB-2 message.
 * @param[in]  index        0-based index of the field to return.
 * @param[out] field        Address of pointer to field.
 * @retval     0            Success. \c *field is set.
 * @retval     G2D_INVALID  Invalid index.
 */
int g2d_getField(
    const DecodedGrib2Msg* const decoded,
    const unsigned               index,
    const Grib2Field** const     field);

#ifdef __cplusplus
}
#endif

#endif

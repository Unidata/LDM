/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file ByteBuf.h
 *
 * This file declares a read-only buffer that can be accessed at the bit-level.
 *
 * @author: Steven R. Emmerson
 */

#ifndef BITBUF_H_
#define BITBUF_H_

#include "grib2.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const unsigned char*        buf;
    size_t                      byteCount;
    size_t                      cursor;
}       ByteBuf;

/**
 * Initializes a byte-buffer.
 *
 * @param[in] bb        Pointer to the byte-buffer to be initialized.
 * @param[in] buf       Pointer to the data buffer. The client must not free
 *                      until the byte-buffer is no longer needed.
 * @param[in] size      Size of the data buffer in bytes.
 */
void bb_init(
    ByteBuf* const              bb,
    const unsigned char* const  buf,
    const size_t                size);

/**
 * Returns a new byte-buffer. The cursor is set to the first byte.
 *
 * @param[in]  buf      Pointer to the data buffer. The client must not free
 *                      until the byte-buffer is no longer needed.
 * @param[in]  size     Size of the data buffer in bytes.
 * @retval     NULL     Memory allocation failure.
 * @return              Pointer to the new byte-buffer. The client should call
 *                      \c bb_free(ByteBuf*) when the byte-buffer is no longer
 *                      needed.
 */
ByteBuf* bb_new(
    const unsigned char* const  buf,
    const size_t                size);

/**
 * Frees a byte-buffer.
 *
 * @param[in] bb        Pointer to the byte-buffer to be freed or NULL.
 */
void bb_free(
    ByteBuf* const       bb);

/**
 * Returns the number of bytes remaining in a byte-buffer from the cursor
 * position.
 *
 * @param[in] bb        Pointer to the byte-buffer.
 * @return              The number of bytes remaining in the buffer from the
 *                      cursor position.
 */
size_t bb_getRemaining(
    const ByteBuf* const bb);

/**
 * Adjusts the byte-cursor of a byte-buffer.
 *
 * @param[in] bb        Pointer to the byte-buffer to have its byte-cursor
 *                      adjusted.
 * @param[in] nbytes    Number of bytes by which to adjust the byte-cursor. May
 *                      be negative.
 * @retval    0         Success.
 * @retval    -1        The cursor cannot be adjusted by the given amount.
 */
int bb_skip(
    ByteBuf* const bb,
    const ssize_t nbytes);

/**
 * Returns the next bytes of a byte-buffer as an integer. Advances the
 * byte-buffer's cursor by the given number of bytes.
 *
 * @param[in]  bb       Pointer to a byte-buffer.
 * @param[in]  nbytes   The length of the integer in bytes.
 * @param[out] value    Pointer to an integer to hold the value.
 * @return     0        Success. \c *value will be set.
 * @retval    -1        The integer extends beyond the data in the byte-buffer.
 *                      \c *value will be unset.
 */
int bb_getInt(
    ByteBuf* const bb,
    const int     nbytes,
    g2int* const  value);

/**
 * Returns the integers corresponding to multiple sets of contiguous bytes in a
 * byte-buffer, starting at the byte-buffer's cursor position. Upon success, the
 * cursor will point one byte beyond the last integer.
 *
 * @param[in]  bb       Pointer to the byte-buffer.
 * @param[in]  nbytes   The length of each integer in bytes.
 * @param[in]  nvalues  The number of integers.
 * @param[out] values   Pointer to an array to hold the integers.
 * @retval     0        Success. \c *value will be set to the given bytes.
 * @retval     -1       Some of the bytes lie outside the buffer. No values have
 *                      been put in the \c values array.
 */
int bb_getInts(
    ByteBuf* const bb,
    const size_t   nbytes,
    const size_t   nvalues,
    g2int* const   values);

/**
 * Sets a byte-buffer's cursor to the start of a sequence of characters. The
 * search begins at the byte-buffer's cursor position.
 *
 * @param[in] bb        Pointer to the byte-buffer.
 * @param[in] chars     Pointer to the 0-terminated sequence of characters to
 *                      find.
 * @param[in] nbytes    Number of bytes to search. May reference beyond the
 *                      byte-buffer data.
 * @retval    0         Success. The cursor now points to the start of the
 *                      character sequence.
 * @retval    -1        Character sequence not found or @code{strlen(chars) ==
 *                      0}. The cursor is unchanged.
 */
int bb_find(
    ByteBuf* const      bb,
    const char* const   chars,
    size_t              nbytes);

/**
 * Returns the value of a byte-buffer's cursor (i.e., the byte-offset of the
 * next byte to be returned).
 *
 * @param[in] bb        Pointer to a byte-buffer.
 * @return              The value of the byte-buffer's cursor.
 */
size_t bb_getCursor(
    const ByteBuf* const bb);

/**
 * Sets the value of the cursor of a byte-buffer (i.e., the byte-offset of the
 * next byte to be returned).
 *
 * @param[in] bb        Pointer to a byte-buffer.
 * @param[in] cursor    The new value for the cursor.
 * @retval    0         Success.
 * @retval    -1        The cursor value lies beyond the byte-buffer's data. The
 *                      byte-buffer's cursor is unchanged.
 * @return              The value of the byte-buffer's cursor.
 */
size_t bb_setCursor(
    ByteBuf* const      bb,
    const size_t        cursor);

#ifdef __cplusplus
}
#endif

#endif /* BITBUF_H_ */

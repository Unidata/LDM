/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file ByteBuf.c
 *
 * This file defines a read-only buffer that can be accessed at the byte-level.
 *
 * @author: Steven R. Emmerson
 */

#include "ByteBuf.h"
#include "grib2.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
    const size_t                size)
{
    bb->buf = buf;
    bb->byteCount = size;
    bb->cursor = 0;
}

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
    const size_t                size)
{
    ByteBuf*     bb = (ByteBuf*)malloc(sizeof(ByteBuf));

    if (bb)
        bb_init(bb, buf, size);

    return bb;
}

/**
 * Frees a byte-buffer.
 *
 * @param[in] bb        Pointer to the byte-buffer to be freed or NULL.
 */
void bb_free(
    ByteBuf* const       bb)
{
    free(bb);
}

/**
 * Returns the number of bytes remaining in a byte-buffer from the cursor
 * position.
 *
 * @param[in] bb        Pointer to the byte-buffer.
 * @return              The number of bytes remaining in the buffer from the
 *                      cursor position.
 */
size_t bb_getRemaining(
    const ByteBuf* const bb)
{
    return bb->byteCount - bb->cursor;
}

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
    const ssize_t nbytes)
{
    ssize_t     newCursor = bb->cursor + nbytes;

    if (0 > newCursor || bb->byteCount < newCursor)
        return -1;

    bb->cursor = newCursor;

    return 0;
}

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
    g2int* const  value)
{
    if (bb->cursor + nbytes > bb->byteCount)
        return -1;

    /*
     * The casts in the following are known to be safe.
     */
    gbit((unsigned char*)bb->buf, value, (g2int)bb->cursor*CHAR_BIT,
            (g2int)nbytes*CHAR_BIT);

    bb->cursor += nbytes;

    return 0;
}

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
    g2int* const   values)
{
    size_t      length = nbytes * nvalues;

    if (0 == length)
        return 0;

    if (bb->cursor + length > bb->byteCount)
        return -1;

    /*
     * The casts in the following are known to be safe.
     */
    gbits((unsigned char*)bb->buf, values, (g2int)(bb->cursor*CHAR_BIT),
            (g2int)(nbytes*CHAR_BIT), 0, (g2int)nvalues);

    bb->cursor += length;

    return 0;
}

#if 0
/**
 * Returns the integers corresponding to multiple sets of contiguous bytes in a
 * byte-buffer, starting at the byte-buffer's cursor position. Upon success, the
 * cursor will point one byte beyond the last integer (NB: the gap following the
 * last integer will not be included).
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
    const size_t  nbytes,
    const size_t  gap,
    const size_t  nvalues,
    g2int* const  values)
{
    if (0 == nvalues)
        return 0;

    size_t      length = nbits + (gap+nbits)*(nvalues-1);

    if (bb->cursor + length > bb->bitCount)
        return -1;

    /*
     * The casts in the following are known to be safe.
     */
    gbits((unsigned char*)bb->buf, values, (g2int)bb->cursor, (g2int)nbits,
            (g2int)gap, (g2int)nvalues);

    bb->cursor += length;

    return 0;
}
#endif

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
    size_t              nbytes)
{
    const size_t        seqLen = strlen(chars);
    int                 i;

    if (0 == seqLen)
        return -1;

    const size_t        start = bb->cursor;
    const size_t        remaining = bb->byteCount - start;

    if (remaining < seqLen)
        return -1;

    if (remaining < nbytes)
        nbytes = remaining;

    for (i = start; i <= start + nbytes - seqLen; i++) {
        int     j;
        for (j = 0; j < seqLen; j++) {
            if (bb->buf[i+j] != chars[j])
                break;
        }
        if (j >= i) {
            bb->cursor = i;
            return 0;
        }
    }

    return -1;
}

/**
 * Returns the value of a byte-buffer's cursor (i.e., the byte-offset of the
 * next byte to be returned).
 *
 * @param[in] bb        Pointer to a byte-buffer.
 * @return              The value of the byte-buffer's cursor.
 */
size_t bb_getCursor(
    const ByteBuf* const bb)
{
    return bb->cursor;
}

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
    const size_t        cursor)
{
    if (cursor > bb->byteCount)
        return -1;

    bb->cursor = cursor;

    return 0;
}

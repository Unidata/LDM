/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file Grib2Info.c
 *
 * This file defines the API for information about a GRIB-2 message.
 *
 * @author: Steven R. Emmerson
 */

#include "ByteBuf.h"
#include "Grib2Info.h"

#include <stdlib.h>

/**
 * Returns a new GRIB-2 information object.
 *
 * @retval 0    Memory allocation failure.
 */
Grib2Info* g2i_new(void)
{
    Grib2Info*  g2i = (Grib2Info*)calloc(1, sizeof(Grib2Info));

    if (g2i) {
        G2SecInfo* const        secInfos = (G2SecInfo*)calloc(2, sizeof(G2SecInfo));

        if (!secInfos) {
            free(g2i);
            g2i = 0;
        }
        else {
            g2i->secInfos = secInfos;
            g2i->maxSecInfos = 2;
            g2i->numSecInfos = 0;
        }
    }

    return g2i;
}

/**
 * Ensures that a GRIB-2 information object can contain information about a
 * given number of sections.
 *
 * @param[in] g2i       Pointer to the GRIB-2 information object.
 * @param[in] num       Number of sections that the object must contain.
 * @retval    0         Success.
 * @retval    1         Memory allocation failure.
 */
int g2i_ensure(
    Grib2Info* const    g2i,
    const size_t        num)
{
    if (num > g2i->maxSecInfos) {
        size_t          max = g2i->maxSecInfos;
        G2SecInfo*      secInfos;

        while (max < num)
            max *= 2;

        secInfos = (G2SecInfo*)realloc(g2i->secInfos, sizeof(G2SecInfo)*max);

        if (!secInfos)
            return 1;

        g2i->secInfos = secInfos;
        g2i->maxSecInfos = max;
    }

    return 0;
}

/**
 * Frees a GRIB-2 information object.
 *
 * @param[in,out] g2i   Pointer to the GRIB-2 information object or NULL.
 */
void g2i_free(
    Grib2Info* const    g2i)
{
    if (g2i) {
        free(g2i->secInfos);
        free(g2i);
    }
}

/**
 * Parses section 0 information from a byte-buffer into a GRIB-2 information
 * object. The number of sections will be set to 1.
 *
 * @param[in,out] g2i           Pointer to the GRIB-2 information object.
 * @param[in,out] bb            Pointer to the byte-buffer. The cursor must be
 *                              at the start of the section (i.e., the next
 *                              byte must be the "G" of the "GRIB" sentinel).
 * @retval        0             Success.
 * @retval        1             Corrupt section.
 */
int g2i_parseSection0(
    Grib2Info* const    g2i,
    ByteBuf* const      bb)
{
    const size_t        cursor = bb_getCursor(bb);
    const size_t        remaining = bb_getRemaining(bb);

    if (bb_skip(bb, 6))
        return 1;
    if (bb_getInt(bb, 1, g2i->sec0))
        return 1;
    if (bb_getInt(bb, 1, g2i->sec0+1))
        return 1;
    if (bb_skip(bb, 4))
        return 1;
    if (bb_getInt(bb, 4, g2i->sec0+2))
        return 1;

    if (remaining < g2i->sec0[2])
        return 1;

    g2i->secInfos[0].offset = cursor;
    g2i->secInfos[0].length = bb_getCursor(bb) - cursor;
    g2i->secInfos[0].secNum = 0;
    g2i->numSecInfos = 1;

    return 0;
}

/**
 * Parses section 1 information from a byte-buffer into a GRIB-2 information
 * object. The number of sections will be set to 2.
 *
 * @param[in,out] g2i           Pointer to the GRIB-2 information object.
 * @param[in,out] bb            Pointer to the byte-buffer. The cursor must be
 *                              at the start of the section.
 * @retval        0             Success.
 * @retval        1             Corrupt section.
 */
int g2i_parseSection1(
    Grib2Info* const    g2i,
    ByteBuf* const      bb)
{
    const size_t        cursor = bb_getCursor(bb);
    size_t              remaining = bb_getRemaining(bb);
    g2int               length;
    g2int               secNum;
    int                 i;
    static const g2int  mapsec1[G2INFO_NUM_SEC1_PARS] =
            {2,2,1,1,1,2,1,1,1,1,1,1,1};

    if (bb_getInt(bb, 4, &length))
        return 1;
    if (16 > length || remaining < length)
        return 1;

    if (bb_getInt(bb, 1, &secNum))
        return 1;
    if (1 != secNum)
        return 1;

    for (i = 0; i < G2INFO_NUM_SEC1_PARS; i++) {
        if (bb_getInt(bb, mapsec1[i], g2i->sec1+i))
            return 1;
    }

    g2i->secInfos[1].offset = cursor;
    g2i->secInfos[1].length = length;
    g2i->secInfos[1].secNum = 1;
    g2i->numSecInfos = 2;

    return 0;
}

/**
 * Appends information about a section. The number of sections will be
 * incremented.
 *
 * @param[in,out] g2i           Pointer to the GRIB-2 information object.
 * @param[in]     offset        Offset, in bytes, to the start of the section.
 * @param[in]     secNum        Number of the section.
 * @param[in]     length        Length, in bytes, of the section.
 * @retval        0             Success.
 * @retval        1             Memory allocation error.
 * @retval        2             @code{secNum == 0 || secNum == 1}.
 */
int g2i_append(
    Grib2Info* const    g2i,
    const size_t        offset,
    const unsigned      secNum,
    const size_t        length)
{
    G2SecInfo*          secInfo;
    const size_t        newNum = g2i->numSecInfos + 1;

    if (secNum == 0 || secNum == 1)
        return 2;

    if (g2i_ensure(g2i, newNum))
        return 1;

    secInfo = g2i->secInfos + g2i->numSecInfos;
    secInfo->offset = offset;
    secInfo->secNum = secNum;
    secInfo->length = length;
    g2i->numSecInfos = newNum;

    return 0;
}

/**
 * Returns the length, in bytes, of a given section.
 *
 * @param[in] g2i       Pointer to the GRIB-2 information object.
 * @param[in] index     0-based index of the given section in the order in which
 *                      it appears in the GRIB-2 message (NB: Not the section
 *                      number). For example: @code{g2i_getLength(4)} returns
 *                      the length of the fifth section in the GRIB-2 message
 *                      regardless of what type of section it is.
 * @param[out] length   Pointer to memory to hold the given section's length.
 * @retval    0         Success. \c *length is set.
 * @retval    1         The given section doesn't exist in \c g2i.
 */
int g2i_getLength(
    const Grib2Info* const      g2i,
    const unsigned              index,
    size_t* const               length)
{
    if (index > g2i->numSecInfos)
        return 1;

    *length = g2i->secInfos[index].length;

    return 0;
}

/**
 * Returns the index of the originating center.
 *
 * @param[in] g2i       Pointer to the GRIB-2 information object.
 */
int g2i_
//                listsec1[0]=Id of originating centre (Common Code Table C-1)
//                listsec1[1]=Id of originating sub-centre (local table)

/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file Grib2SectionInfo.h
 *
 * This file declares the API for information about a GRIB-2 message.
 *
 * @author: Steven R. Emmerson
 */

#ifndef GRIB2__INFO_H
#define GRIB2__INFO_H

#include "grib2.h"

typedef struct {
    size_t      offset;
    size_t      length;
    g2int       secNum;
}       G2SecInfo;

typedef struct {
    /*
     * sec0[0]  Discipline-GRIB Master Table Number (see Code Table 0.0)
     * sec0[1]  GRIB Edition Number (currently 2)
     * sec0[2]  Length of GRIB message
     */
    g2int       sec0[3];
    /*
     * sec1[0]  Id of originating centre (Common Code Table C-1)
     * sec1[1]  Id of originating sub-centre (local table)
     * sec1[2]  GRIB Master Tables Version Number (Code Table 1.0)
     * sec1[3]  GRIB Local Tables Version Number
     * sec1[4]  Significance of Reference Time (Code Table 1.1)
     * sec1[5]  Reference Time - Year (4 digits)
     * sec1[6]  Reference Time - Month
     * sec1[7]  Reference Time - Day
     * sec1[8]  Reference Time - Hour
     * sec1[9]  Reference Time - Minute
     * sec1[10] Reference Time - Second
     * sec1[11] Production status of data (Code Table 1.2)
     * sec1[12] Type of processed data (Code Table 1.3)
     */
#define G2INFO_NUM_SEC1_PARS    13
    g2int       sec1[G2INFO_NUM_SEC1_PARS];
    g2int       numFields;
    g2int       numLocal;
    size_t      numSecInfos;
    size_t      maxSecInfos;
    G2SecInfo*  secInfos;
}       Grib2Info;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the number of sections in a GRIB-2 information object.
 *
 * @param[in] g2i       Pointer to the GRIB-2 information object.
 */
size_t g2i_getSectionCount(
        Grib2Info* const        g2i);
#define g2i_getSectionCount(g2i)        ((g2i)->numSecInfos)


#ifdef __cplusplus
}
#endif

#endif

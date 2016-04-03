/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: pdb.c
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the product-definition block of a NESDIS-
 * formatted product.
 */

#include "config.h"

#include "decode.h"
#include "ldm.h"
#include "log.h"
#include "nbs_status.h"
#include "pdb.h"

/**
 * @retval    NBS_STATUS_INVAL  Invalid header
 */
nbs_status_t pdb_decode(
        pdb_t* const restrict         pdb,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    /*
     * Always 512 bytes according to
     * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> -- but
     * only the first 46 are decoded.
     */
    if (nbytes < 46) {
        log_add("Product-definition block shorter than 46 bytes: %u",
                nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        pdb->source = buf[0];
        pdb->creating_entity = buf[1];
        pdb->sector_id = buf[2];
        pdb->physical_element = buf[3];
        pdb->num_logical_recs = decode_uint16(buf+4);
        pdb->logical_rec_size = decode_uint16(buf+6);
        pdb->year = ((buf[8] > 70) ? 1900 : 2000) + buf[8];
        pdb->month = buf[9];
        pdb->day = buf[10];
        pdb->hour = buf[11];
        pdb->minute = buf[12];
        pdb->second = buf[13];
        pdb->centisecond = buf[14];
        pdb->nx = decode_uint16(buf+16);
        pdb->ny = decode_uint16(buf+18);
        pdb->image_res = buf[41];
        pdb->is_compressed = buf[42];
        pdb->version = buf[43];
        pdb->length = decode_uint16(buf+44);
        *nscanned = pdb->length;
        status = 0;
    }
    return status;
}

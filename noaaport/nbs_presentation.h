/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: prod_proc.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the presentation layer of the NOAAPort
 * Broadcast System (NBS).
 */

#ifndef NOAAPORT_NBS_PRESENTATION_H_
#define NOAAPORT_NBS_PRESENTATION_H_

typedef struct nbsp {

} *nbsp_t;


#ifdef __cplusplus
    extern "C" {
#endif

nbsp_goes_east(buf, nbytes);
nbsp_goes_west(buf, nbytes);
nbsp_nongoes(buf, nbytes);
nbsp_nwstg(buf, nbytes);
nbsp_nexrad(buf, nbytes);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_PRESENTATION_H_ */

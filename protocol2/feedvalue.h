/*
 *   Copyright 2003, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: feedvalue.h,v 1.1.2.1 2009/08/14 14:57:59 steve Exp $ */

#ifndef _FEEDVALUE_H
#define _FEEDVALUE_H

#include <stddef.h>

#ifdef __cplusplus
    extern "C" {
#endif


typedef enum {
    FV_INIT = 1,
    FV_NOMEM,
    FV_READY,
    FV_NOT_FOUND,
    FV_DUP_ID
} FeedValueError;


#ifdef __cplusplus
    }
#endif

#endif

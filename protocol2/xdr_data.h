/**
 *   Copyright 2016, University Corporation for Atmospheric Research. All
 *   rights reserved. See file COPYRIGHT in the top-level source-directory for
 *   copying and redistribution conditions.
 */

#ifndef _XDR_DATA_H
#define	_XDR_DATA_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void*	xd_getBuffer(size_t size);
void*	xd_getNextSegment(size_t size);
void	xd_reset();

#ifdef __cplusplus
}
#endif

#endif

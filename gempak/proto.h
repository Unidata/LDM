/************************************************************************
 * proto.h                                                              *
 *                                                                      *
 * This include file contains function prototypes for all the c code in *
 * the common nawips libraries.                                         *
 **                                                                     *
 * E. Safford/GSC       10/00   Created                                 *
 * A. Hardy/NCEP	 7/03   Added proto_textlib			*
 ***********************************************************************/


#ifndef	PROTO
#define PROTO

#ifndef Boolean
typedef char Boolean;
#endif

#ifndef Cardinal
typedef unsigned int    Cardinal;
#endif

#include "proto_gemlib.h"
#include "proto_cgemlib.h"
/*#include "proto_textlib.h"
#include "proto_nmaplib.h"
#include "proto_xw.h"*/


#endif 		/* PROTO */

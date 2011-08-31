/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#define _XOPEN_SOURCE 500

#include <stddef.h>
#include <syslog.h>

#include "config.h"
#include "getFacilityName.h"      /* eat own dog food */

typedef struct _code {
    char*   c_name;
    int     c_val;
} CODE;

static CODE facilitynames[] = {
    { "local0", LOG_LOCAL0 },
    { "local1", LOG_LOCAL1 },
    { "local2", LOG_LOCAL2 },
    { "local3", LOG_LOCAL3 },
    { "local4", LOG_LOCAL4 },
    { "local5", LOG_LOCAL5 },
    { "local6", LOG_LOCAL6 },
    { "local7", LOG_LOCAL7 },
    { NULL, -1 }
};

/**
 * Returns the name corresponding to a logging facility.
 *
 * @return The name corresponding to the given logging facility.
 */
const char* getFacilityName(
    const unsigned    facility) /**< [in] Integer representation of the logging
                                  *  facility */
{
    int         i;
    const char* name = NULL;

    for (i = 0; NULL != facilitynames[i].c_name; i++) {
        if (facility == facilitynames[i].c_val)
            return facilitynames[i].c_name;
    }

    return "unknown";
}

/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file server_info.c
 * @author Steven R. Emmerson
 *
 * This file implements server contact information.
 */

#include "config.h"

#include "log.h"
#include "server_info.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <rpc.h>

/**
 * The definition of server contact information.
 */
struct server_info {
    char*          id; /* hostname or formatted IP address */
    unsigned short port;
};

/**
 * Returns a new server contact information object.
 *
 * @param[in] id    Name or formatted IP address of the host running the server.
 * @param[in] port  Port number of the server.
 * @retval NULL     Error. \c log_add() called.
 * @return          Pointer to a new server contact information object. The
 *                  client should call \c serverInfo_free() when it is no longer
 *                  needed.
 */
ServerInfo*
serverInfo_new(
    const char* const    id,
    const unsigned short port)
{
    ServerInfo* si = LOG_MALLOC(sizeof(ServerInfo),
            "server contact information");

    if (si) {
        si->id = strdup(id);

        if (!si->id) {
            LOG_ADD1("Couldn't duplicate server identifier \"%s\"", id);
            free(si);
            si = NULL;
        }
        else {
            si->port = port;
        }
    }

    return si;
}

/**
 * Frees a server contact information object.
 *
 * @param[in,out] serverInfo  Pointer to server contact information to be freed
 *                            or NULL.
 */
void
serverInfo_free(
    ServerInfo* const si)
{
    if (si) {
        free(si->id);
        free(si);
    }
}

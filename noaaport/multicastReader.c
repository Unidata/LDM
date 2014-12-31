/**
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 *
 *   @file   multicastReader.c
 *   @author Steven E. Emmerson
 *
 *   This file implements a reader of a NOAAPORT multicast channel.
 */
#include <config.h>

#include "log.h"
#include "fifo.h"
#include "noaaport_socket.h" /* Eat own dog food */
#include "reader.h"

#include <unistd.h>

/**
 * Returns a new NOAAPORT reader.
 *
 * This function is thread-safe.
 *
 * @param[our] reader     The returned reader.
 * @param[in]  mcastSpec  IPv4 address of the NOAAPORT multicast group.
 * @param[in]  ifaceSpec  IPv4 address of the interface on which to listen for
 *                        multicast packets or NULL to listen on all available
 *                        interfaces.
 * @param[in]  fifo       Pointer to the FIFO into which to write data.
 * @retval     0          Success. `*reader` is set.
 * @retval     1          Usage failure. `log_start()` called.
 * @retval     2          System failure. `log_start()` called.
 */
int mcastReader_new(
    Reader** const      reader,
    const char* const   mcastSpec,
    const char* const   ifaceSpec,
    Fifo* const         fifo)
{
    int socket;
    int status = ns_init(&socket, mcastSpec, ifaceSpec);

    if (0 == status) {
        /*
         * The maximum IPv4 UDP payload is 65507 bytes. The maximum observed UDP
         * payload, however, should be 5232 bytes, which is the maximum amount
         * of data in a NESDIS frame (5152 bytes) plus the overhead of the 3 SBN
         * protocol headers: frame level header (16 bytes) + product definition
         * header (16 bytes) + AWIPS product specific header (48 bytes). The
         * maximum size of an ethernet jumbo frame is around 9000 bytes.
         * Consequently, the maximum amount to read in a single call is
         * conservatively set to 10000 bytes. 2014-12-30.
         */
        status = readerNew(socket, fifo, 10000, reader);
        if (status) {
            LOG_ADD0("Couldn't create new reader object");
            (void)close(socket);
        }
    } // `socket` set

    return status;
}

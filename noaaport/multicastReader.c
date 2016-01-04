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

#include "mylog.h"
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
 * @retval     1          Usage failure. `mylog_add()` called.
 * @retval     2          System failure. `mylog_add()` called.
 */
int mcastReader_new(
    Reader** const      reader,
    const char* const   mcastSpec,
    const char* const   ifaceSpec,
    Fifo* const         fifo)
{
    int socket;
    int status = nportSock_init(&socket, mcastSpec, ifaceSpec);

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
         *
         * Reverted to 65507 bytes because the number of frames missed by Chico
         * increased greatly relative to Lenny after the maximum read size was
         * changed from 65507 to 10000 bytes. Could it be that NOAAPORT is using
         * large UDP packets and depending on IP fragmentation? That seems
         * inconsistent, however, with dvbs_multicast(1) use of 10000 bytes in
         * its call to recvfrom(2). 2015-01-3.
         */
        status = readerNew(socket, fifo, 65507, reader);
        if (status) {
            mylog_add("Couldn't create new reader object");
            (void)close(socket);
        }
    } // `socket` set

    return status;
}

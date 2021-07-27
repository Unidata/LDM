/**
 * This program generates an invalid FMTP protocol data unit (PDU) for use by
 * iperf(1) to maliciously attack an FMTP receiver.
 *
 *  Created on: Dec 16, 2020
 *      Author: Steven R. Emmerson
 */

#include <FmtpBase.h>
#include <iostream>
#include <netinet/in.h>

static const char* progname = NULL;

static bool
decodeArgs(int          argc,
           const char** argv)
{
    progname = argv[0];

    return argc == 1;
}

static void
usage()
{
    std::cerr << "Usage: " << progname << " >file";
}

static void
writePdu()
{
    // Write header
    union {
        uint32_t u32;
        uint16_t u16;
        char     c[1];
    } u;

    u.u32 = htonl(1);                    // Product index
    std::cout.write(u.c, sizeof(u.u32));
    u.u32 = htonl(0);                    // Byte-offset of data-segment
    std::cout.write(u.c, sizeof(u.u32));
    char payload[MAX_FMTP_PAYLOAD];
    u.u16 = htons(sizeof(payload));      // Identify as data segment
    std::cout.write(u.c, sizeof(u.u16));
    u.u16 = htons(FMTP_MEM_DATA);        // Identify as data segment
    std::cout.write(u.c, sizeof(u.u16));

    // Write payload
    std::cout.write(payload, sizeof(payload));

    // Write HMAC
    char hmac[MAC_SIZE];
    std::cout.write(hmac, sizeof(hmac));
}

/**
 * Write an invalid FMTP PDU to the standard output stream.
 *
 * @param[in] argc  Number of arguments
 * @param[in] argv  Argument vector
 * @retval    0     Success
 * @retval    1     Failure
 */
int
main(   int          argc,
        const char** argv)
{
    int status = 1; // Failure

    if (!decodeArgs(argc, argv)) {
        usage();
    }
    else {
        writePdu();
    }

    return status;
}


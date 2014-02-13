/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file vcmtp_c_api.c
 *
 * This file defines the C API to the Virtual Circuit Multicast Transport
 * Protocol, VCMTP.
 *
 * @author: Steven R. Emmerson
 */

#include "vcmtp_c_api.h"
#include "PerFileNotifier.h"
#include <exception>
#include <stdlib.h>
#include <VCMTPReceiver.h>

/**
 * The VCMTP C Receiver data-object.
 */
struct vcmtp_c_receiver {
    /**
     * The per-file notifier of file-events seen by the VCMTP layer.
     */
    PerFileNotifier*    notifier;
    /**
     * The VCMTP-layer receiver.
     */
    VCMTPReceiver*      receiver;
};

/**
 * Initializes a VCMTP C Receiver.
 *
 * @param[in,out] cReceiver         The VCMTP C Receiver to initialize.
 * @param[in]     bof_func          Function to call when the VCMTP layer has
 *                                  seen a beginning-of-file.
 * @param[in]     eof_func          Function to call when the VCMTP layer has
 *                                  completely received a file.
 * @param[in]     missed_file_func  Function to call when a file is missed by
 *                                  the VCMTP layer.
 * @param[in]     extra_arg         Extra argument to pass to the above
 *                                  functions. May be 0.
 * @throws        std:exception     If the VCMTP C Receiver can't be
 *                                  initialized.
 */
static void vcmtp_receiver_init(
    VcmtpCReceiver* const cReceiver,
    const BofFunc         bof_func,
    const EofFunc         eof_func,
    const MissedFileFunc  missed_file_func,
    void* const           extra_arg)
{
    cReceiver->notifier = new PerFileNotifier(bof_func, eof_func,
            missed_file_func, extra_arg);
    try {
        cReceiver->receiver = new VCMTPReceiver(*cReceiver->notifier);
    }
    catch (const std::exception& e) {
        delete cReceiver->notifier;
        throw e;
    }
}

VcmtpCReceiver* vcmtp_receiver_new(
    const BofFunc               bof_func,
    const EofFunc               eof_func,
    const MissedFileFunc        missed_file_func,
    void* const                 extra_arg)
{
    VcmtpCReceiver*      cReceiver =
            static_cast<VcmtpCReceiver*>(malloc(sizeof(VcmtpCReceiver)));

    if (cReceiver) {
        try {
            vcmtp_receiver_init(cReceiver, bof_func, eof_func, missed_file_func,
                    extra_arg);
            return cReceiver;
        }
        catch (const std::exception& e) {
            delete cReceiver;
        }
    }

    return 0;
}

void vcmtp_receiver_free(
    VcmtpCReceiver* const       cReceiver)
{
    delete cReceiver->receiver;
    delete cReceiver->notifier;
    free(cReceiver);
}

int vcmtp_receiver_join_group(
    VcmtpCReceiver* const       cReceiver,
    const char* const           addr,
    const unsigned short        port)
{
    return cReceiver->receiver->JoinGroup(std::string(addr), port);
}

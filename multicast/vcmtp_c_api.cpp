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
#include <VCMTPReceiver.h>

vcmtp_receiver* vcmtp_receiver_new(BofFunc bof_func, EofFunc eof_func,
        MissedFileFunc missed_file_func, void* extra_arg) {
    return new VCMTPReceiver(PerFileNotifier::get_instance(bof_func, eof_func,
            missed_file_func, extra_arg));
}

int vcmtp_receiver_join_group(vcmtp_receiver* const self,
        const char* const addr, const unsigned short port) {
    return static_cast<VCMTPReceiver*>(self)->JoinGroup(std::string(addr), port);
}

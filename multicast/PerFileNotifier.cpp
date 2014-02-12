/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerFileNotifier.cpp
 *
 * This file defines a class that notifies the receiving application about file
 * events on a per-file basis.
 *
 * @author: Steven R. Emmerson
 */

#include "PerFileNotifier.h"
#include "vcmtp_c_api.h"
#include <stdexcept>
#include <strings.h>

PerFileNotifier& PerFileNotifier::get_instance(BofFunc bof_func,
        EofFunc eof_func, MissedFileFunc missed_file_func, void* extra_arg) {
    return *new PerFileNotifier(bof_func, eof_func, missed_file_func,
            extra_arg);
}

PerFileNotifier::PerFileNotifier(BofFunc bof_func, EofFunc eof_func,
        MissedFileFunc missed_file_func, void* extra_arg)
:   bof_func(bof_func),
    eof_func(eof_func),
    missed_file_func(missed_file_func),
    extra_arg(extra_arg)
{
    if (!bof_func)
        throw std::invalid_argument("Null argument: bof_func");
    if (!eof_func)
        throw std::invalid_argument("Null argument: eof_func");
    if (!missed_file_func)
        throw std::invalid_argument("Null argument: missed_file_func");
}

static const file_metadata* file_metadata_init(file_metadata& info,
        const VcmtpSenderMessage& msg) {
    info.length = msg.data_len;
    info.time = msg.time_stamp;
    (void)strncpy(info.name, msg.text, sizeof(info.name));
    info.name[sizeof(info.name)-1] = 0;
    return &info;
}

bool PerFileNotifier::notify_of_bof(VcmtpSenderMessage& msg) {
    file_metadata       info;

    return bof_func(extra_arg, file_metadata_init(info, msg));
}

void PerFileNotifier::notify_of_eof(VcmtpSenderMessage& msg) {
    file_metadata       info;

    eof_func(extra_arg, file_metadata_init(info, msg));
}

void PerFileNotifier::notify_of_missed_file(VcmtpSenderMessage& msg) {
    file_metadata       info;

    missed_file_func(extra_arg, file_metadata_init(info, msg));
}

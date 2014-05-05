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
#include "log.h"

#include <vcmtp.h>

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>

PerFileNotifier::PerFileNotifier(
    BofFunc             bof_func,
    EofFunc             eof_func,
    MissedFileFunc      missed_file_func,
    void*               obj)
:
    bof_func(bof_func),
    eof_func(eof_func),
    missed_file_func(missed_file_func),
    obj(obj)
{
    if (!bof_func)
        throw std::invalid_argument("Null argument: bof_func");
    if (!eof_func)
        throw std::invalid_argument("Null argument: eof_func");
    if (!missed_file_func)
        throw std::invalid_argument("Null argument: missed_file_func");
}

/**
 * Notifies the receiving application about a file that is about to be received
 * by the VCMTP layer.
 *
 * @param[in,out] file_entry            The VCMTP file-entry.
 * @retval        0                     Success.
 * @throws        std::runtime_error    if the receiving application indicates
 *                                      an error.
 */
void PerFileNotifier::notify_of_bof(VcmtpFileEntry& file_entry) const
{
    if (bof_func(obj, &file_entry)) {
        throw std::runtime_error(std::string(
                "Error notifying receiving application of beginning of file"));
    }
}

void PerFileNotifier::notify_of_eof(VcmtpFileEntry& file_entry) const
{
    if (eof_func(obj, &file_entry)) {
        throw std::runtime_error(std::string(
                "Error notifying receiving application of end of file"));
    }
}

void PerFileNotifier::notify_of_missed_file(VcmtpFileEntry& file_entry) const
{
    missed_file_func(obj, &file_entry);
}

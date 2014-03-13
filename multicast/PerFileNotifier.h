/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerFileNotifier.h
 *
 * This file declares the API for a class that notifies the receiving
 * application of events on a per-file basis.
 *
 * @author: Steven R. Emmerson
 */

#ifndef PER_FILE_NOTIFIER_H_
#define PER_FILE_NOTIFIER_H_

#include "vcmtp_c_api.h"
#include <ReceivingApplicationNotifier.h>
#include <vcmtp.h>
#include <VcmtpFileEntry.h>

#include <memory>

class PerFileNotifier: public ReceivingApplicationNotifier {
public:
    /**
     * Constructs from the notification functions.
     *
     * @param[in] bof_func              Function to call when the beginning of
     *                                  a file has been seen by the VCMTP layer.
     * @param[in] eof_func              Function to call when a file has been
     *                                  completely received by the VCMTP layer.
     * @param[in] missed_file_func      Function to call when a file is missed
     *                                  by the VCMTP layer.
     * @param[in] obj                   Relevant object in the receiving
     *                                  application. May be NULL.
     * @throws    std::invalid_argument if @code{!bof_func || !eof_func ||
     *                                  !missed_file_func}
     */
    PerFileNotifier(
            BofFunc         bof_func,
            EofFunc         eof_func,
            MissedFileFunc  missed_file_func,
            void*           obj);

    ~PerFileNotifier() {}
    void        notify_of_bof(VcmtpFileEntry& file_entry) const;
    void        notify_of_eof(VcmtpFileEntry& file_entry) const;
    void        notify_of_missed_file(VcmtpFileEntry& file_entry) const;

private:
    /**
     * Function to call when a beginning-of-file has been seen by the VCMTP
     * layer.
     */
    BofFunc             bof_func;
    /**
     * Function to call when a file has been completely received by the VCMTP
     * layer.
     */
    EofFunc             eof_func;
    /**
     * Function to call when a files is missed by the VCMTP layer.
     */
    MissedFileFunc      missed_file_func;
    /**
     * Extra argument passed to the above functions.
     */
    void*               obj;
};

#endif /* PER_FILE_NOTIFIER_H_ */

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

class PerFileNotifier: public ReceivingApplicationNotifier {
public:
    /**
     * Returns a new instance.
     * @param[in] bof_func              Function to call when the beginning of
     *                                  a file has been seen by the VCMTP layer.
     * @param[in] eof_func              Function to call when a file has been
     *                                  completely received by the VCMTP layer.
     * @param[in] missed_file_func      Function to call when a file is missed
     *                                  by the VCMTP layer.
     * @param[in] extra_arg             Optional argument passed to the above
     *                                  functions.
     * @return                          A new instance. Should be deleted by the
     *                                  client when no longer needed.
     * @throws    std::invalid_argument if @code{!bof_func || !eof_func ||
     *                                  !missed_file_func}
     */
    static PerFileNotifier& get_instance(
            BofFunc             bof_func,
            EofFunc             eof_func,
            MissedFileFunc      missed_file_func,
            void*               extra_arg = 0);

    ~PerFileNotifier() {}

private:
    /**
     * Constructs. This class is not designed for inheritance.
     *
     * @param[in] bof_func              Function to call when the beginning of
     *                                  a file has been seen by the VCMTP layer.
     * @param[in] eof_func              Function to call when a file has been
     *                                  completely received by the VCMTP layer.
     * @param[in] missed_file_func      Function to call when a file is missed
     *                                  by the VCMTP layer.
     * @throws    std::invalid_argument if @code{!bof_func || !eof_func}
     */
    PerFileNotifier(
            BofFunc         bof_func,
            EofFunc         eof_func,
            MissedFileFunc  missed_file_func,
            void*           extra_arg);

    bool        notify_of_bof(VcmtpSenderMessage& msg);
    void        notify_of_eof(VcmtpSenderMessage& msg);
    void        notify_of_missed_file(VcmtpSenderMessage& msg);

    /**
     * Function to call when a beginning-of-file has been seen by the VCMTP
     * layer.
     */
    BofFunc     bof_func;
    /**
     * Function to call when a file has been completely received by the VCMTP
     * layer.
     */
    EofFunc     eof_func;
    /**
     * Function to call when a files is missed by the VCMTP layer.
     */
    MissedFileFunc      missed_file_func;
    /**
     * Extra argument passed to the above functions.
     */
    void*       extra_arg;
};

#endif /* PER_FILE_NOTIFIER_H_ */

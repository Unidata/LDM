/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file:   FmtpFileEntry.h
 * @author: Steven R. Emmerson
 *
 * This file declares a class that represents a file that's being received by
 * the FMTP layer. Instances contain metadata about their file and a
 * BofResponse that disposes of incoming data.
 */

#ifndef FILEENTRY_H_
#define FILEENTRY_H_

#include "BofResponse.h"
#include "fmtp.h"

#include <memory>

class FmtpFileEntry {
public:
    FmtpFileEntry(const struct FmtpSenderMessage& msg)
        : msg(msg), bofResponse(0) {}
    ~FmtpFileEntry() {
        if (bofResponse != BofResponse::getIgnore())
            delete bofResponse;
    }
    /**
     * Indicates if the file is wanted or not.
     *
     * @retval true  The file is wanted.
     * @retval false The file is not wanted.
     */
    bool isWanted() const {
        return bofResponse->isWanted();
    }
    /**
     * Returns the size of the file in bytes.
     *
     * @return the size of the file in bytes.
     */
    size_t getSize() const {
        return msg.data_len;
    }
    /**
     * Returns the identifier of the file.
     *
     * @return the identifier of the file.
     */
    const uint32_t getFileId() const {
        return msg.session_id;
    }
    /**
     * Returns the name of the file.
     *
     * @return the name of the file.
     */
    const char* getName() const {
        return msg.text;
    }
    /**
     * Sets the beginning-of-file response to ignore the file.
     */
    void setBofResponseToIgnore() {
        bofResponse = BofResponse::getIgnore();
    }
    /**
     * Sets the response from the receiving application to the beginning-of-file
     * notification by the FMTP layer.
     *
     * @param[in] bofResponse  The receiving application's response to the BOF
     *                         notification by the FMTP layer. Will be deleted
     *                         by this instance's destructor.
     */
    void setBofResponse(const BofResponse* bofResponse) {
        this->bofResponse = bofResponse;
    }
    /**
     * Returns the response from the receiving application to the
     * beginning-of-file notification by the FMTP layer.
     *
     * @return The receiving application's response to the BOF notification by
     *         the FMTP layer.
     */
    const BofResponse* getBofResponse() {
        return bofResponse;
    }
    /**
     * Indicates if the file transfer mode is to memory.
     *
     * @retval true   The file transfer mode is to memory.
     * @retval false  The file transfer mode is not to memory.
     */
    bool isMemoryTransfer() const {
        return msg.msg_type == MEMORY_TRANSFER_START ||
               msg.msg_type == TCP_MEMORY_TRANSFER_START;
    }
    /**
     * Receives a multicast data packet.
     * @param[in] sock  The socket on which to receive the data.
     */
    void receiveMulticast(int sock);
    /**
     * Receives a unicast data packet.
     * @param[in] sock  The socket on which to receive the data.
     */
    void receiveUnicast(int sock);

private:
    const struct FmtpSenderMessage     msg;
    const BofResponse*                  bofResponse;
};

#endif /* FILEENTRY_H_ */

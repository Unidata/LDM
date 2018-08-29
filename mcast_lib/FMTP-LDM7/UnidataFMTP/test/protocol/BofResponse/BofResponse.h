/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file BofResponse.h
 *
 * This file declares the response by a receiving application to a
 * beginning-of-file notification from the FMTP layer.
 *
 * @author: Steven R. Emmerson
 */

#ifndef BOFRESPONSE_H_
#define BOFRESPONSE_H_

#include <fmtp.h>

#include <stdlib.h>
#include <sys/types.h>

class BofResponse {
public:
    BofResponse(bool isWanted) : is_wanted(isWanted) {};
    virtual ~BofResponse() {};
    /**
     * Indicates of the data is wanted or not.
     * @retval true  if and only if the data is wanted.
     */
    bool isWanted() const {
        return is_wanted;
    }
    /**
     * Disposes of a portion of the file that's being received.
     * @param[in] sock               The socket on which to receive the data.
     * @param[in] offset             The offset, in bytes, from the start of the
     *                               file to the first byte read from the
     *                               socket.
     * @param[in] nbytes             The amount of data to receive in bytes.
     * @retval    0                  The socket is closed.
     * @return                       The number of bytes read from the socket.
     * @throws    invalid_argument   If @code{offset < 0 ||
     *                               nbytes > FMTP_PACKET_LEN ||
     *                               offset+nbytes >} size of file.
     * @throws    runtime_exception  If an I/O error occurs on the socket.
     */

    /*
     * Since BofResponse is an abstract class, I have to modify this virtual
     * method dispose() and implement an empty function for it in order to
     * compile. The line below is its original interface. dispose() should be
     * restored to virtual method and I'll use mocking lib to test this class.
     */
    //virtual size_t dispose(int sock, off_t offset, size_t nbytes) const = 0;
    virtual size_t dispose(int sock, off_t offset, size_t nbytes) const {};
    /*
     * Returns a beginning-of-file response that will cause the file to be
     * ignored.
     * @return A BOF response that will cause the file to be ignored.
     */
    static const BofResponse* getIgnore();

private:
    bool is_wanted;
};


/**
 * BOF response for a transfer to memory.
 */
class MemoryBofResponse : public BofResponse {
public:
    MemoryBofResponse(char* buf, size_t size, bool isWanted);
    virtual ~MemoryBofResponse() {};
    size_t dispose(int sock, off_t offset, size_t size) const;
    /**
     * Returns the memory buffer.
     * @return Pointer to the memory buffer.
     */
    char* getBuf() const { return buf; }

private:
    char*       buf;
    size_t      size;
};


#endif /* BOFRESPONSE_H_ */

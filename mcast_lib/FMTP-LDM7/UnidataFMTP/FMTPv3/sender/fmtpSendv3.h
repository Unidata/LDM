/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      fmtpSendv3.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Oct 16, 2014
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Define the interfaces of FMTPv3 sender side method function.
 *
 * Sender side of FMTPv3 protocol. It multicasts packets out to multiple
 * receivers and retransmits missing blocks to the receivers.
 */


#ifndef FMTP_SENDER_FMTPSENDV3_H_
#define FMTP_SENDER_FMTPSENDV3_H_


#include <pthread.h>
#include <sys/types.h>
#include <atomic>
#include <exception>
#include <list>
#include <map>
#include <set>

#include "ProdIndexDelayQueue.h"
#include "../RateShaper/RateShaper.h"
#include "RetxThreads.h"
#include "SendProxy.h"
#include "senderMetadata.h"
#include "../SilenceSuppressor/SilenceSuppressor.h"
#include "TcpSend.h"
#include "UdpSend.h"
#include "fmtpBase.h"
#include "Serializer.h"


class fmtpSendv3;

/**
 * To contain multiple types of necessary information and transfer to the
 * StartRetxThread() as one single parameter.
 */
struct StartRetxThreadInfo
{
    /**
     * A pointer to the fmtpSendv3 instance itself which starts the
     * StartNewRetxThread().
     */
    fmtpSendv3*    retxmitterptr;
    /** The particular retx socket this running thread is listening on */
    int             retxsockfd;
};


/**
 * To contain multiple types of necessary information and transfer to the
 * StartTimerThread() as one single parameter.
 */
struct StartTimerThreadInfo
{
    uint32_t        prodindex; /*!< product index */
    fmtpSendv3*    sender;    /*!< a pointer to the fmtpSendv3 instance */
};


/**
 * sender side class handling the multicasting, retransmission and timeout.
 */
class fmtpSendv3
{
    class UdpSerializer : public Serializer
    {
        UdpSend*     udpSend;                  /// UDP sender
        char         buf[MAX_FMTP_PACKET_LEN]; /// Network byte-order buffer
        char* const  end;                      /// One beyond `buf`
        char*        start;                    /// Start of current segment
        char*        next;                     /// Next position in current segment
        struct iovec iovec[IOV_MAX+1];         /// Gather write vector
        int          iovIndex;                 /// Current `iovec` element

        void  reset();
        void  nextBufSeg();
        void  vetSeg();
        void  add(const void* value, unsigned nbytes);

    protected:
        void add(const uint16_t value);
        void add(const uint32_t value);

    public:
        using Serializer::encode;

        UdpSerializer(UdpSend* udpSend);

        void encode(const void* bytes, unsigned nbytes);

        void flush();
    };

public:
    explicit fmtpSendv3(
                 const char*           tcpAddr,
                 const unsigned short  tcpPort,
                 const char*           mcastAddr,
                 const unsigned short  mcastPort,
                 SendProxy*            notifier = NULL,
                 const unsigned char   ttl = 1,
                 const std::string     ifAddr = "0.0.0.0",
                 const uint32_t        initProdIndex = 0,
                 const float           tsnd = 10.0);
    ~fmtpSendv3();

    /* ----------- testapp-specific APIs begin ----------- */
    /* performs reset for each run in multiple runs */
    void           clearRuninProdSet(int run);
    /*
     * gets notification of a complete and ACKed file,
     * should be used together with silence suppressor.
     */
    uint32_t       getNotify();
    /*
     * releases memory of a complete and ACKed file,
     * should be used together with silence suppressor.
     */
    uint32_t       releaseMem();
    /* ----------- testapp-specific APIs end ----------- */

    unsigned short getTcpPortNum();
    uint32_t       getNextProdIndex() const {return prodIndex;}
    uint32_t       sendProduct(void* data, uint32_t dataSize);
    uint32_t       sendProduct(void* data, uint32_t dataSize, void* metadata,
                               uint16_t metaSize);
    void           SetSendRate(uint64_t speed);
    /** Sender side start point, the first function to be called */
    void           Start();
    /** Sender side stop point */
    void           Stop();

private:
    /**
     * Adds and entry for a data-product to the retransmission set.
     *
     * @param[in] data       The data-product.
     * @param[in] dataSize   The size of the data-product in bytes.
     * @param[in] metadata   Product-specific metadata
     * @param[in] metaSize   Size of `metadata` in bytes
     * @param[in] startTime  Time product given to FMTP layer for transmission
     * @return              The corresponding retransmission entry.
     * @throw std::runtime_error  if a retransmission entry couldn't be created.
     */
    RetxMetadata* addRetxMetadata(void* const data, const uint32_t dataSize,
                                  void* const metadata, const uint16_t metaSize,
                                  const struct timespec* startTime);
    static uint32_t blockIndex(uint32_t start) {return start/FMTP_DATA_LEN;}
    /** new coordinator thread */
    static void* coordinator(void* ptr);
    /**
     * Handles a retransmission request.
     *
     * @param[in] recvheader  FMTP header of the retransmission request.
     * @param[in] retxMeta    Associated retransmission entry.
     */
    void handleRetxReq(FmtpHeader* const  recvheader,
                       RetxMetadata* const retxMeta, const int sock);
    /**
     * Handles a notice from a receiver that a data-product has been completely
     * received.
     *
     * @param[in] recvheader  The FMTP header of the notice.
     * @param[in] retxMeta    The associated retransmission entry.
     * @param[in] sock        The receiver's socket.
     */
    void handleRetxEnd(FmtpHeader* const  recvheader,
                       RetxMetadata* const retxMeta, const int sock);
    /**
     * Handles a notice from a receiver that BOP for a product is missing.
     *
     * @param[in] recvheader  The FMTP header of the notice.
     * @param[in] retxMeta    The associated retransmission entry.
     * @param[in] sock        The receiver's socket.
     */
    void handleBopReq(FmtpHeader* const  recvheader,
                      RetxMetadata* const retxMeta, const int sock);
    /**
     * Handles a notice from a receiver that EOP for a product is missing.
     *
     * @param[in] recvheader  The FMTP header of the notice.
     * @param[in] retxMeta    The associated retransmission entry.
     * @param[in] sock        The receiver's socket.
     */
    void handleEopReq(FmtpHeader* const  recvheader,
                      RetxMetadata* const retxMeta, const int sock);
    /** new timer thread */
    void RunRetxThread(int retxsockfd);
    /**
     * Rejects a retransmission request from a receiver.
     *
     * @param[in] prodindex  Product-index of the request.
     * @param[in] sock       The receiver's socket.
     */
    void rejRetxReq(const uint32_t prodindex, const int sock);
    /**
     * Retransmits data to a receiver.
     *
     * @param[in] recvheader  The FMTP header of the retransmission request.
     * @param[in] retxMeta    The associated retransmission entry.
     * @param[in] sock        The receiver's socket.
     */
    void retransmit(const FmtpHeader* const recvheader,
                    const RetxMetadata* const retxMeta, const int sock);
    /**
     * Retransmits BOP packet to a receiver.
     *
     * @param[in] recvheader  The FMTP header of the retransmission request.
     * @param[in] retxMeta    The associated retransmission entry.
     * @param[in] sock        The receiver's socket.
     */
    void retransBOP(const FmtpHeader* const  recvheader,
                    const RetxMetadata* const retxMeta, const int sock);
    /**
     * Retransmits EOP packet to a receiver.
     *
     * @param[in] recvheader  The FMTP header of the retransmission request.
     * @param[in] sock        The receiver's socket.
     */
    void retransEOP(const FmtpHeader* const  recvheader, const int sock);
    void SendBOPMessage(uint32_t prodSize, void* metadata,
                        const uint16_t metaSize,
                        const struct timespec& startTime);
    /**
     * Multicasts the data of a data-product.
     *
     * @param[in] data      The data-product.
     * @param[in] dataSize  The size of the data-product in bytes.
     * @throw std::runtime_error  if an I/O error occurs.
     */
    void sendEOPMessage();
    void sendData(void* data, uint32_t dataSize);
    /**
     * Sets the retransmission timeout parameters in a retransmission entry.
     *
     * @param[in] senderProdMeta  The retransmission entry.
     */
    void setTimerParameters(RetxMetadata* const senderProdMeta);
    void StartNewRetxThread(int newtcpsockfd);
    /** new retranmission thread */
    static void* StartRetxThread(void* ptr);
    void taskExit(const std::runtime_error&);
    void timerThread();
    /** a wrapper to call the actual fmtpSendv3::timerThread() */
    static void* timerWrapper(void* ptr);
    /* Prevent copying because it's meaningless */
    fmtpSendv3(fmtpSendv3&);
    fmtpSendv3& operator=(const fmtpSendv3&);
    void WriteToLog(const std::string& content);


    uint32_t            prodIndex;
    /** underlying udp layer instance */
    UdpSend*            udpsend;
    /** underlying tcp layer instance */
    TcpSend*            tcpsend;
    /** maintaining metadata for retx use. */
    senderMetadata*     sendMeta;
    /** sending application callback hook */
    SendProxy*          notifier;
    ProdIndexDelayQueue timerDelayQ;
    pthread_t           coor_t;
    pthread_t           timer_t;
    /** tracks all the dynamically created retx threads */
    RetxThreads         retxThreadList;
    std::mutex          linkmtx;
    uint64_t            linkspeed;
    std::mutex          exitMutex;
    std::exception_ptr  except;
    bool                exceptIsSet;
    RateShaper          rateshaper;
    std::mutex          notifyprodmtx;
    std::mutex          notifycvmtx;
    uint32_t            notifyprodidx;
    std::condition_variable notify_cv;
    std::condition_variable memrelease_cv;
    /* SilenceSuppressor is only used for testapp */
    SilenceSuppressor*  suppressor;
    /* sender maximum retransmission timeout */
    double              tsnd;


    /* member variables for measurement use only */
    bool                txdone;
    std::chrono::high_resolution_clock::time_point start_t;
    std::chrono::high_resolution_clock::time_point end_t;
    /* member variables for measurement use ends */

    /// Serializes objects for multicasting
    UdpSerializer       udpSerializer;
};


#endif /* FMTP_SENDER_FMTPSENDV3_H_ */

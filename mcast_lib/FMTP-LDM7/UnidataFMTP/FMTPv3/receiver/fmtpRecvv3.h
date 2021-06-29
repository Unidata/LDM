/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      fmtpRecvv3.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Oct 17, 2014
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
 * @brief     Define the interfaces of FMTPv3 receiver.
 *
 * Receiver side of FMTPv3 protocol. It handles incoming multicast packets
 * and issues retransmission requests to the sender side.
 */


#ifndef FMTP_RECEIVER_FMTPRECVV3_H_
#define FMTP_RECEIVER_FMTPRECVV3_H_


#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <list>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "FmtpBase.h"
#include "Measure.h"
#include "ProdSegMNG.h"
#include "RecvProxy.h"
#include "TcpRecv.h"
#include "UdpRecv.h"


class fmtpRecvv3;

struct StartTimerInfo
{
    uint32_t     prodindex;  /*!< product index */
    float        seconds;    /*!< product index */
    fmtpRecvv3* receiver;   /*!< a pointer to the fmtpRecvv3 instance */
};

struct ProdTracker
{
    uint32_t     prodsize;
    void*        prodptr;
    uint32_t     seqnum;
    uint16_t     paylen;
    uint32_t     numRetrans;
};

typedef std::unordered_map<uint32_t, ProdTracker> TrackerMap;
typedef std::unordered_map<uint32_t, bool> EOPStatusMap;

class fmtpRecvv3 {
	/**
	 * A thread-safe queue of messages from this receiver to the FMTP sender.
	 */
    class MsgQueue
    {
        typedef std::queue<INLReqMsg>   Queue;
        typedef std::mutex              Mutex;
        typedef std::lock_guard<Mutex>  Guard;
        typedef std::unique_lock<Mutex> Lock;
        typedef std::condition_variable Cond;

        Queue         queue;
        mutable Mutex mutex;
        mutable Cond  cond;

    public:
        /**
         * Constructs.
         */
        MsgQueue();

        /**
         * Adds a message to the tail of the queue.
         *
         * @param[in] msg  Message to be added
         * @threadsafety   Safe
         */
        void push(const INLReqMsg& msg);

        /**
         * Adds a message to the tail of the queue.
         *
         * @param[in] msg  Message to be added
         * @threadsafety   Safe
         */
        template<typename... Args>
        void emplace(Args&&... args);

        /**
         * Returns a reference to the message at the head of the queue.
         *
         * @return Reference to the message at the head of the queue
         * @threadsafety   Safe
         */
        const INLReqMsg& front() const;

        /**
         * Deletes the message at the head of the queue.
         * @threadsafety   Safe
         */
        void pop();
    };

    /**
     * Tracks the highest product-index (modulo (UINT32_MAX+1)) received so far.
     */
    class HighestProdIndex
    {
        using Mutex = std::mutex;
        using Guard = std::lock_guard<Mutex>;

        mutable Mutex mutex;      /// Concurrent access mutex
        uint32_t      prodIndex;  /// Index of multicast product
        bool          indexSet;   /// Product-index is set?

    public:
        HighestProdIndex();

        /**
         * No copy or move construction because there should only be one
         * instance per FMTP receiver.
         */
        HighestProdIndex(const HighestProdIndex& that) =delete;
        HighestProdIndex(HighestProdIndex&& that) =delete;

        /**
         * No copy or move assignment because there should only be one instance
         * per FMTP receiver.
         */
        HighestProdIndex& operator=(const HighestProdIndex& rhs) =delete;
        HighestProdIndex& operator=(HighestProdIndex&& rhs) =delete;

        uint32_t setIfHigher(const uint32_t prodIndex);

        uint32_t get() const;
    };

    FmtpBase                fmtpBase; ///< Runtime constants
    /* Sender VLAN Unique IP address */
    std::string             tcpAddr;
    /* Sender FMTP TCP Connection port number */
    unsigned short          tcpPort;
    std::string             mcastAddr;
    unsigned short          mcastPort;
    /* IP address of the default interface */
    std::string             ifAddr;
    int                     retxSock;
    struct sockaddr_in      mcastgroup;
    /* callback function of the receiving application */
    RecvProxy*              notifier;
    TcpRecv*                tcprecv;
    /* a map from prodindex to struct ProdTracker */
    TrackerMap              trackermap;
    std::mutex              trackermtx;
    /* eliminate race conditions between mcast and retx */
    std::mutex              antiracemtx;
    /* a map from prodindex to EOP arrival status */
    EOPStatusMap            EOPmap;
    std::mutex              EOPmapmtx;
    ProdSegMNG*             pSegMNG;
    MsgQueue                msgQueue;
    /* track all the missing BOP until received */
    std::unordered_set<uint32_t> misBOPset;
    std::mutex              BOPSetMtx;
    /* Retransmission request thread */
    pthread_t               retx_rq;
    /* Retransmission receive thread */
    pthread_t               retx_t;
    /* Multicast receiver thread */
    pthread_t               mcast_t;
    /* BOP timer thread */
    pthread_t               timer_t;
    /* a queue containing timerParam structure for each product */
    std::queue<timerParam>  timerParamQ;
    std::condition_variable timerQfilled;
    std::mutex              timerQmtx;
    std::condition_variable timerWake;
    std::mutex              timerWakemtx;
    std::mutex              exitMutex;
    std::condition_variable exitCond;
    bool                    stopRequested;
    std::exception_ptr      except;
    std::mutex              linkmtx;
    /* max link speed up to 18000 Pbps */
    uint64_t                linkspeed;
    std::atomic_flag        retxHandlerCanceled;
    std::atomic_flag        mcastHandlerCanceled;
    std::mutex              notifyprodmtx;
    uint32_t                notifyprodidx;
    std::condition_variable notify_cv;
    UdpRecv                 udpRecv;
    // Open lower index for BOP requests
    HighestProdIndex           openLeftIndex;

    /* member variables for measurement use only */
    Measure*                measure;
    /* member variables for measurement use ends */

    std::string getMacKey();

    bool addUnrqBOPinSet(uint32_t prodindex);
    /**
     * Parse BOP message and call notifier to notify receiving application.
     *
     * @param[in] header          Header associated with the packet.
     * @param[in] payload         Pointer to payload of FMTP packet.
     * @retval    `true`          FMTP message is valid
     * @retval    `false`         FMTP message is not valid
     * @throw std::runtime_error  if the payload is too small.
     */
    void BOPHandler(const FmtpHeader& header,
                    const char* const payload);
    void checkPayloadLen(const FmtpHeader& header, const size_t nbytes);
    void clearEOPStatus(const uint32_t prodindex);
    /**
     * Decodes a FMTP packet header.
     *
     * @param[in]  packet         The raw packet.
     * @param[in]  nbytes         The size of the raw packet in bytes.
     * @param[out] header         The decoded packet header.
     * @param[out] payload        Payload of the packet.
     * @throw std::runtime_error  if the packet is too small.
     * @throw std::runtime_error  if the packet has in invalid payload length.
     */
    void decodeHeader(char* const packet, FmtpHeader& header);
    void updateAckedProd(const uint32_t prodindex);
    void doneWithProd(
            const bool       inTracker,
            struct timespec& now,
            const uint32_t   prodindex,
            const uint32_t   numRetrans);
    void EOPHandler(const FmtpHeader& header);
    bool getEOPStatus(const uint32_t prodindex);
    bool hasLastBlock(const uint32_t prodindex);
    void initEOPStatus(const uint32_t prodindex);
    void joinGroup(
            std::string          srcAddr,
            std::string          mcastAddr,
            const unsigned short mcastPort);
    /**
     * Handles a multicast BOP message given a peeked-at FMTP header.
     *
     * @pre                           The multicast socket contains a FMTP BOP
     *                                packet.
     * @param[in] header              The associated, already-decoded FMTP header.
     * @param[in] payload             FMTP packet payload of `header.payloadlen`
     *                                bytes
     * @throw     std::system_error   if an error occurs while reading the socket.
     * @throw     std::runtime_error  if the packet is invalid.
     */
    void mcastBOPHandler(const FmtpHeader& header, const char* payload);
    /**
     * Indicates if a product-index equals the index of the last multicast
     * product.
     *
     * @param[in] prodIndex  Product-index to compare
     * @retval    `true`     Product-index does equal last multicast product
     * @retval    `false`    Product-index does not equal last multicast product
     */
    bool equalsMcastProdId(uint32_t prodIndex);
    /**
     * Sets the multicast sequence number, which is the offset, in bytes, to the
     * start of the current multicast data-segment.
     *
     * @param[in] seqNum  Sequence number
     */
    void setMcastSeqNum(const uint32_t seqNum);
    /**
     * Returns the index of the last multicast product.
     *
     * @return               Index of last multicast product
     */
    uint32_t getMcastProdId() const;
    /**
     * Exchanges the index of the last multicast product.
     *
     * @param[in] prodIndex  Product-index to set
     * @return               Index of last multicast product if set; otherwise,
     *                       `prodIndex`
     */
    uint32_t setMcastProdId(uint32_t prodIndex);
    /**
     * Handles a multicast data-packet.
     *
     * @param[in] header           Peeked-at FMTP header
     * @param[in] payload          `header.payloadlen` bytes of payload
     * @throw std::runtime_error   Error occurred while reading the socket
     */
    void mcastDataHandler(const FmtpHeader& header,
                          const char*       payload);
    void mcastHandler();
    void mcastEOPHandler(const FmtpHeader& header);
    /**
     * Pushes a request for a data-packet onto the retransmission-request queue.
     *
     * @param[in] prodindex  Index of the associated data-product.
     * @param[in] seqnum     Sequence number of the data-packet.
     * @param[in] datalen    Amount of data in bytes.
     */
    void pushDataReq(const uint32_t prodindex, const uint32_t seqnum,
                            const uint16_t datalen);
    /**
     * Pushes a request for a BOP-packet onto the retransmission-request queue.
     *
     * @param[in] prodindex  Index of the associated data-product.
     */
    void pushBopReq(const uint32_t prodindex);
    /**
     * Pushes a request for a EOP-packet onto the retransmission-request queue.
     *
     * @param[in] prodindex  Index of the associated data-product.
     */
    void pushEopReq(const uint32_t prodindex);
    void retxHandler();
    void retxRequester();
    bool rmMisBOPinSet(uint32_t prodindex);
    /**
     * Handles a retransmitted BOP message.
     *
     * @param[in] header          Header associated with the packet.
     * @param[in] FmtpPacketData  Pointer to payload of FMTP packet.
     * @retval    `true`          Message is valid
     * @retval    `false`         Message is not valid
     */
    bool retxBOPHandler(const FmtpHeader& header,
                        const char* const  FmtpPacketData);
    void retxEOPHandler(const FmtpHeader& header);
    /**
     * Reads the data portion of a FMTP data-packet into the location specified
     * by the receiving application.
     *
     * @pre                       The socket contains a FMTP data-packet.
     * @param[in] header          The associated, peeked-at and decoded header.
     * @param[in] payload         `header.payloadlen` bytes of payload
     * @param[in] prodptr         Destination for data
     * @param[in] prodsize        Size of data in bytes
     * @throw std::system_error   if an error occurs while reading the multicast
     *                            socket.
     */
    void readMcastData(const FmtpHeader& header,
                       const char*       payload,
                       void* const       prodptr,
                       const uint32_t    prodsize);
    /**
     * Requests data-packets that lie between the last previously-received
     * data-packet of the current data-product and its most recently-received
     * data-packet.
     *
     * @param[in] prodindex Product index.
     * @param[in] seqnum  The most recently-received data-packet of the current
     *                    data-product.
     */
    void requestAnyMissingData(const uint32_t prodindex,
                               const uint32_t mostRecent);
    /**
     * Requests BOP packets for a prodindex interval.
     *
     * @param[in] openleft   Open left end of the prodindex interval.
     * @param[in] openright  Open right end of the prodindex interval.
     */
    void requestBops(const uint32_t openleft, const uint32_t openright);
    void requestBopsExcl(const uint32_t prodindex);
    void requestBopsIncl(const uint32_t prodindex);
    /**
     * request EOP retx if EOP is not received yet and return true if
     * the request is sent out. Otherwise, return false.
     * */
    bool reqEOPifMiss(const uint32_t prodindex);
    static void* runTimerThread(void* ptr);
    bool sendBOPRetxReq(uint32_t prodindex);
    bool sendEOPRetxReq(uint32_t prodindex);
    bool sendDataRetxReq(uint32_t prodindex, uint32_t seqnum,
                         uint16_t payloadlen);
    bool sendRetxEOP(const uint32_t prodindex);
    bool sendRetxEnd(uint32_t prodindex) const;
    static void*  StartRetxRequester(void* ptr);
    static void*  StartRetxHandler(void* ptr);
    static void*  StartMcastHandler(void* ptr);
    void StartRetxProcedure();
    void startTimerThread();
    void setEOPStatus(const uint32_t prodindex);
    void timerThread();
    void taskExit(const std::exception_ptr& e);
    void WriteToLog(const std::string& content);
    void stopJoinRetxRequester();
    void stopJoinRetxHandler();
    void stopJoinTimerThread();
    void stopJoinMcastHandler();

public:
    /**
     * Constructs.
     *
     * @param[in] tcpAddr       Sender TCP unicast address for retransmission.
     * @param[in] tcpPort       Sender TCP unicast port for retransmission.
     * @param[in] mcastAddr     UDP multicast address for receiving data products.
     * @param[in] mcastPort     UDP multicast port for receiving data products.
     * @param[in] notifier      Callback function to notify receiving application
     *                          of incoming Begin-Of-Product messages.
     * @param[in] ifAddr        IPv4 address of local interface receiving
     *                          multicast packets and retransmitted data-blocks.
     */
    fmtpRecvv3(const std::string    tcpAddr,
               const unsigned short tcpPort,
               const std::string    mcastAddr,
               const unsigned short mcastPort,
               RecvProxy*           notifier = NULL,
               const std::string    ifAddr = "0.0.0.0");
    ~fmtpRecvv3();

    uint32_t getNotify();
    void SetLinkSpeed(uint64_t speed);
    void Start();
    void Stop();
};


#endif /* FMTP_RECEIVER_FMTPRECVV3_H_ */

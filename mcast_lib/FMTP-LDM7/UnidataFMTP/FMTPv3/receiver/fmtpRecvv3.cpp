/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      fmtpRecvv3.cpp
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
 * @brief     Define the entity of FMTPv3 receiver side method function.
 *
 * Receiver side of FMTPv3 protocol. It handles incoming multicast packets
 * and issues retransmission requests to the sender side.
 */


#include "fmtpRecvv3.h"
#ifdef LDM_LOGGING
    #include "log.h"
#endif
#include "PubKeyCrypt.h"

#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <cinttypes>
#include <math.h>
#include <memory.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>
#include <fstream>
#include <iostream>
#include <system_error>

#define Frcv 20

fmtpRecvv3::MsgQueue::MsgQueue()
        : queue{}
    , mutex{}
    , cond{}
{}

void fmtpRecvv3::MsgQueue::push(const INLReqMsg& msg)
{
    Guard guard(mutex);
    queue.push(msg);
    cond.notify_one();
}

template<typename... Args>
void fmtpRecvv3::MsgQueue::emplace(Args&&... args)
{
    Guard guard(mutex);
    queue.emplace(args...);
    cond.notify_one();
}

const INLReqMsg& fmtpRecvv3::MsgQueue::front() const
{
    Lock lock(mutex);

    while (queue.empty())
        cond.wait(lock);

    return queue.front();
}

void fmtpRecvv3::MsgQueue::pop()
{
    Guard guard(mutex);
        queue.pop();
}

/**
 * Constructs.
 */
fmtpRecvv3::HighestProdIndex::HighestProdIndex()
    : mutex()
    , prodIndex(0)
    , indexSet(false)
{}

/**
 * Sets the product-index if it's greater (modulo arithmetic) than the previous
 * one. Returns the previous value.
 *
 * @param[in] prodindex          Product-index to be considered
 * @return                       On first call, given product-index minus one;
 *                               otherwise, previously-saved value
 */
uint32_t fmtpRecvv3::HighestProdIndex::setIfHigher(const uint32_t prodindex)
{
    Guard    guard(mutex);
    uint32_t prevProdIndex = indexSet ? this->prodIndex : prodindex - 1;

    if (indexSet && (prodindex - this->prodIndex > UINT32_MAX/2)) {
#       ifdef LDM_LOGGING
            log_warning("Retrograde product-index: new=%" PRIu32
                    ", previous=%" PRIu32, prodindex, this->prodIndex);
#       endif
    }
    else {
        this->prodIndex = prodindex;
        indexSet = true;
    }
    // NB: Product-index set from now on

    return prevProdIndex;
}

/**
 * Returns the product-index.
 *
 * @return                  Product-index
 * @throw std::logic_error  `set()` hasn't been called
 */
uint32_t fmtpRecvv3::HighestProdIndex::get() const {
    Guard guard(mutex);
    if (!indexSet)
        throw std::logic_error("set() hasn't been called");
    return prodIndex;
}

/**
 * Constructs the receiver side instance (for integration with LDM).
 *
 * @param[in] tcpAddr       Sender TCP unicast address for retransmission.
 * @param[in] tcpPort       Sender TCP unicast port for retransmission.
 * @param[in] mcastAddr     UDP multicast address for receiving data products.
 * @param[in] mcastPort     UDP multicast port for receiving data products.
 * @param[in] notifier      Callback function to notify receiving application
 *                          of incoming Begin-Of-Product messages.
 * @param[in] ifAddr        IPv4 address of local interface for receiving
 *                          multicast packets and retransmitted data-blocks.
 */
fmtpRecvv3::fmtpRecvv3(
    const std::string    tcpAddr,
    const unsigned short tcpPort,
    const std::string    mcastAddr,
    const unsigned short mcastPort,
    RecvProxy*           notifier,
    const std::string    ifAddr)
:
    fmtpBase{},
    tcpAddr(tcpAddr),
    tcpPort(tcpPort),
    mcastAddr(mcastAddr),
    mcastPort(mcastPort),
    ifAddr(ifAddr),
    retxSock(-1),
    mcastgroup(),
    notifier(notifier),
    tcprecv(new TcpRecv(tcpAddr, tcpPort, inet_addr(ifAddr.c_str()))),
    trackermap(),
    trackermtx(),
    antiracemtx(),
    EOPmap(),
    EOPmapmtx(),
    pSegMNG(new ProdSegMNG()),
    msgQueue(),
    misBOPset(),
    BOPSetMtx(),
    retx_rq(),
    retx_t(),
    mcast_t(),
    timer_t(),
    timerParamQ(),
    timerQfilled(),
    timerQmtx(),
    timerWake(),
    timerWakemtx(),
    exitMutex(),
    exitCond(),
    stopRequested(false),
    except(),
    linkmtx(),
    linkspeed(20000000),
    retxHandlerCanceled(ATOMIC_FLAG_INIT),
    mcastHandlerCanceled(ATOMIC_FLAG_INIT),
    notifyprodmtx(),
    notifyprodidx(0),
    notify_cv(),
    udpRecv(),
    openLeftIndex(),
    measure(new Measure())
{
}


/**
 * Destroys the receiver side instance. Releases the resources.
 *
 * @param[in] none
 */
fmtpRecvv3::~fmtpRecvv3()
{
    Stop();
    #ifdef LDM_LOGGING
    	log_debug("Closing retransmission socket");
    #endif
    (void)close(retxSock); // failure is irrelevant
    {
        std::lock_guard<std::mutex> lock(BOPSetMtx);
        misBOPset.clear();
    }
    {
        std::lock_guard<std::mutex> lock(trackermtx);
        trackermap.clear();
    }
    delete tcprecv;
    delete pSegMNG;
    delete measure;
}


/**
 * Gets the notified product index.
 *
 * @return    The product index of the latest completed product.
 */
uint32_t fmtpRecvv3::getNotify()
{
    std::unique_lock<std::mutex> lock(notifyprodmtx);
    notify_cv.wait(lock);
    return notifyprodidx;
}


/**
 * A public setter of link speed. The setter is thread-safe, but a recommended
 * way is to set the link speed before the receiver starts. Due to the feature
 * of virtual circuits, the link speed won't change when it's set up. So the
 * link speed remains for the whole life of the receiver. As the possible link
 * speed nowadays could possibly be 10Gbps or even higher, a 64-bit unsigned
 * integer is used to hold the value, which can be up to 18000 Pbps.
 *
 * @param[in] speed                 User-specified link speed
 */
void fmtpRecvv3::SetLinkSpeed(uint64_t speed)
{
    std::lock_guard<std::mutex> lock(linkmtx);
    linkspeed = speed;
}


/**
 * Connect to sender via TCP socket, join given multicast group (defined by
 * mcastAddr:mcastPort) to receive multicasting products. Start retransmission
 * handler and retransmission requester and also start multicast receiving
 * thread. Doesn't return until `fmtpRecvv3::Stop` is called or an exception
 * is thrown. Exceptions will be caught and all the threads will be terminated
 * by the exception handling code.
 *
 * @throw std::runtime_error  If a retransmission-reception thread can't be
 *                            started.
 * @throw std::runtime_error  If a retransmission-request thread can't be
 *                            started.
 * @throw std::runtime_error  If a multicast-receiving thread couldn't be
 *                            created.
 * @throw std::runtime_error  If the multicast group couldn't be joined.
 * @throw std::runtime_error  If an I/O error occurs.
 */
void fmtpRecvv3::Start()
{
    /** connect to the sender */
    /*
     * Apparently, just because an AL2S VLAN has just been provisioned for this
     * host, that doesn't mean the VLAN works just yet.
     */
    const int timeout = 120;
    const int interval = 5;
    for (int t = 0; t <= timeout; t += interval) {
        try {
            tcprecv->Init();
            #if !defined(NDEBUG) && defined(LDM_LOGGING)
                log_debug("Connected to FMTP server after %d seconds", t);
            #endif
            break;
        }
        catch (const std::system_error& ex) {
            if (t == timeout || ::sleep(interval))
                throw; // Time is up or a signal interrupted `sleep()`
        }
    }

    const auto macKey = getMacKey();
    udpRecv = UdpRecv(tcpAddr, mcastAddr, mcastPort, ifAddr, macKey),

    StartRetxProcedure();
    startTimerThread();

    int status = pthread_create(&mcast_t, NULL, &fmtpRecvv3::StartMcastHandler,
                                this);
    if (status) {
        Stop();
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::Start(): Couldn't start "
                "multicast-receiving thread, failed with status = "
                + std::to_string(status));
    }

    {
        std::unique_lock<std::mutex> lock(exitMutex);
        while (!stopRequested && !except)
            exitCond.wait(lock);
    }
    stopJoinRetxRequester();
    stopJoinRetxHandler();
    stopJoinTimerThread();
    stopJoinMcastHandler();

    {
        std::lock_guard<std::mutex> lock(exitMutex);
        if (except) {
            /**
             * The actual exception object that exception_ptr is pointing to
             * gets rethrown. There is no copying here, which means the
             * exception message will not be sliced by rethrow_exception().
             * Also, exception_ptr points to the actual exception by reference,
             * so there will be no copying caused by assigning exception_ptr.
             */
            std::rethrow_exception(except);
        }
    }
}


/**
 * Stops a running FMTP receiver. Returns immediately. Idempotent.
 *
 * @pre                `fmtpRecvv3::Start()` was previously called.
 * @asyncsignalsafety  Unsafe
 */
void fmtpRecvv3::Stop()
{
    {
        std::lock_guard<std::mutex> lock(exitMutex);
        stopRequested = true;
        exitCond.notify_one();
    }

    int prevState;

    /*
     * Prevent a thread on which `taskExit()` is executing from canceling itself
     * before it cancels others
     */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &prevState);
    (void)pthread_setcancelstate(prevState, &prevState);
}

/**
 * Uses the TCP connection with the FMTP sender to obtain the MAC key of
 * multicast FMTP messages. Sends a public-key nonce to the publisher using the
 * TCP connection, reads the encrypted MAC key from the connection, and decrypts
 * the MAC key using the private key nonce.
 *
 * @return                    Key for computing message authentication codes of
 *                            multicast FMTP messages
 * @throw std::system_error   I/O failure
 * @throw std::runtime_error  EOF
 * @throw std::runtime_error  OpenSSL failure
 */
std::string fmtpRecvv3::getMacKey()
{
    PrivateKey privateKey{};
    const auto pubKey = privateKey.getPubKey();

    #ifdef LDM_LOGGING
        log_debug("Sending %s-byte public key",
                std::to_string(pubKey.size()).c_str());
    #endif
    tcprecv->write(pubKey);

    #ifdef LDM_LOGGING
        log_debug("Receiving encrypted MAC key");
    #endif
    std::string cipherKey;
    tcprecv->read(cipherKey);

    #ifdef LDM_LOGGING
        log_debug("Decrypting %s-byte MAC key",
                std::to_string(cipherKey.size()).c_str());
    #endif
    std::string plainKey;
    privateKey.decrypt(cipherKey, plainKey);

    return plainKey;
}

/**
 * Add the unrequested BOP identified by the given prodindex into the list.
 * If the BOP is already in the list, return with a false. If it's not, add
 * it into the list and return with a true.
 *
 * @param[in] prodindex        Product index of the missing BOP
 * @retval    `true`           Index was added to the list
 * @retval    `false`          Index was not added to the list
 */
bool fmtpRecvv3::addUnrqBOPinSet(uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(BOPSetMtx);
    return misBOPset.insert(prodindex).second;
}


/**
 * Handles a multicast BOP message given its peeked-at and decoded FMTP header.
 * Nothing happens if the message is invalid.
 *
 * Executed on the multicast receiving thread.
 *
 * @pre                       The multicast socket contains a FMTP BOP packet.
 * @param[in] header          The associated, peeked-at and already-decoded
 *                            FMTP header.
 * @param[in] payload         `header.payloadlen` bytes of payload
 * @throw std::system_error   if an error occurs while reading the socket.
 * @throw std::runtime_error  Product-index gap is impossibly large
 */
void fmtpRecvv3::mcastBOPHandler(const FmtpHeader& header,
                                 const char*       payload)
{
    #ifdef MODBASE
        uint32_t tmpidx = header.prodindex % MODBASE;
    #else
        uint32_t tmpidx = header.prodindex;
    #endif

    #ifdef DEBUG2
        std::string debugmsg = "[MCAST BOP] Product #" +
            std::to_string(tmpidx);
        debugmsg += ": BOP received from multicast.";
        std::cout << debugmsg << std::endl;
        WriteToLog(debugmsg);
    #endif

    if (BOPHandler(header, payload)) {
        /*
         * Detects completely missing products by checking the consistency
         * between the previously received product-index and the
         * currently-received one.
         */
        // Also sets future open left product-index for BOP requests
        requestBopsExcl(header.prodindex);
    }
}


/**
 * Handles a retransmitted BOP message given its FMTP header.
 *
 * @param[in] header          Header associated with the packet.
 * @param[in] FmtpPacketData  Pointer to payload of FMTP packet.
 * @retval    `true`          Message is valid
 * @retval    `false`         Message is not valid
 */
bool fmtpRecvv3::retxBOPHandler(const FmtpHeader& header,
                                const char* const FmtpPacketData)
{
    #ifdef MODBASE
        uint32_t tmpidx = header.prodindex % MODBASE;
    #else
        uint32_t tmpidx = header.prodindex;
    #endif

    #ifdef DEBUG2
        std::string debugmsg = "[RETX BOP] Product #" +
            std::to_string(tmpidx);
        debugmsg += ": BOP received from unicast.";
        std::cout << debugmsg << std::endl;
        WriteToLog(debugmsg);
    #endif

    return BOPHandler(header, FmtpPacketData);
}


/**
 * Parse BOP message and call notifier to notify receiving application. Does
 * nothing if the FMTP message is invalid.
 *
 * @param[in] header          Header associated with the packet.
 * @param[in] payload         Pointer to payload of FMTP packet.
 * @retval    `true`          Message is valid
 * @retval    `false`         Message is not valid
 */
bool fmtpRecvv3::BOPHandler(const FmtpHeader& header,
                            const char* const payload)
{
    bool     isValid = false;
    void*    prodptr = NULL;
    BOPMsg   BOPmsg;
    /*
     * Every time a new BOP arrives, save the msg to check following data
     * packets
     */
    if (header.payloadlen < BOPMsg::HEADER_SIZE) {
        #ifdef LDM_LOGGING
            log_warning("Payload is too small");
        #endif
    }
    else {
        const char* wire = payload;

        const uint32_t* uint32p = (const uint32_t*)wire;
        BOPmsg.startTime[0] = ntohl(*uint32p++);
        BOPmsg.startTime[1] = ntohl(*uint32p++);
        BOPmsg.startTime[2] = ntohl(*uint32p++);

        BOPmsg.prodsize = ntohl(*uint32p++);

        const uint16_t* uint16p = (const uint16_t*)uint32p;
        BOPmsg.metasize = ntohs(*uint16p++);

        if (header.payloadlen < BOPMsg::HEADER_SIZE + BOPmsg.metasize) {
            #ifdef LDM_LOGGING
                log_warning("Metadata is too big");
            #endif
        }
        else {
            wire = (const char*)uint16p;
            (void)memcpy(BOPmsg.metadata, wire, BOPmsg.metasize);

            #if !defined(NDEBUG) && defined(LDM_LOGGING)
                log_debug("Received BOP {header={index=%lu, payload=%u}, "
                        "bop={prodsize=%lu, metasize=%u}}",
                        (unsigned long)header.prodindex, header.payloadlen,
                        (unsigned long)BOPmsg.prodsize, BOPmsg.metasize);
            #endif

            /*
             * Here a strict check is performed to make sure the information in
             * trackermap and the product segment-manager would not be
             * overwritten by duplicate BOP. By design, a product should exist
             * in both the trackermap and the product segment-manager or
             * neither, which is the condition of executing all the
             * initialization. Also, startProd() will only be called for a fresh
             * new BOP. All the duplicate calls will be suppressed.
             */
            bool insertion = pSegMNG->addProd(header.prodindex, BOPmsg.prodsize);
            bool inTracker;
            {
                std::lock_guard<std::mutex> lock(trackermtx);
                inTracker = trackermap.count(header.prodindex);
            }
            if (insertion && !inTracker) {
                if(notifier) {
                    struct timespec startTime;
                    startTime.tv_sec =
                            (static_cast<uint64_t>(BOPmsg.startTime[0]) << 32) |
                            BOPmsg.startTime[1];
                    startTime.tv_nsec = BOPmsg.startTime[2];
                    notifier->startProd(startTime, header.prodindex,
                            BOPmsg.prodsize, BOPmsg.metadata, BOPmsg.metasize,
                            &prodptr);
                }

                /* Atomic insertion for BOP of new product */
                {
                    ProdTracker tracker = {BOPmsg.prodsize, prodptr, 0, 0, 0};
                    std::lock_guard<std::mutex> lock(trackermtx);
                    trackermap[header.prodindex] = tracker;
                }

                /* forcibly terminate the previous timer */
                timerWake.notify_all();

                initEOPStatus(header.prodindex);

                /*
                 * Since the receiver timer starts after BOP is received, the
                 * RTT is not affecting the timer model. Sleeptime here means
                 * the estimated reception time of this product. Thus, the only
                 * thing needs to be considered is the transmission delay, which
                 * can be calculated as product size over link speed. Besides, a
                 * little more extra time would be favorable to tolerate
                 * possible fluctuation.
                 */
                double sleeptime = 0.0;
                {
                    std::lock_guard<std::mutex> lock(trackermtx);
                    if (trackermap.count(header.prodindex)) {
                        sleeptime =
                                Frcv * ((double)trackermap[header.prodindex].prodsize /
                                (double)linkspeed);
                    }
                    else {
                        throw std::runtime_error("fmtpRecvv3::BOPHandler(): "
                                "Error accessing newly added BOP in trackermap.");
                    }
                }
                /* add the new product into timer queue */
                {
                    std::lock_guard<std::mutex> lock(timerQmtx);
                    timerParam timerparam = {header.prodindex, sleeptime};
                    timerParamQ.push(timerparam);
                    timerQfilled.notify_all();
                }

                isValid = true;
            } // Product wasn't in segment-manager and isn't in trackermap
            else {
                #if !defined(NDEBUG) && defined(LDM_LOGGING)
                    log_info("Received duplicate BOP for product %lu",
                            static_cast<unsigned long>(header.prodindex));
                #else
                    std::cout << "fmtpRecvv3::BOPHandler(): duplicate BOP for product #"
                            << header.prodindex << "received." << std::endl;
                #endif
            }

            #ifdef MODBASE
                uint32_t tmpidx = header.prodindex % MODBASE;
            #else
                uint32_t tmpidx = header.prodindex;
            #endif

            #ifdef MEASURE
                {
                    std::lock_guard<std::mutex> lock(trackermtx);
                    if (trackermap.count(header.prodindex)) {
                        ProdTracker tracker = trackermap[header.prodindex];
                        measure->insert(header.prodindex, tracker.prodsize);
                    }
                    else {
                        throw std::runtime_error("fmtpRecvv3::BOPHandler(): "
                                "[Measure] Error accessing newly added BOP");
                    }
                }
                std::string measuremsg = "[MEASURE] Product #" +
                    std::to_string(tmpidx);
                measuremsg += ": BOP is received. Product size = ";
                measuremsg += std::to_string(BOPmsg.prodsize);
                measuremsg += ", Metadata size = ";
                measuremsg += std::to_string(BOPmsg.metasize);
                std::cout << measuremsg << std::endl;
                WriteToLog(measuremsg);
            #endif
        } // Metadata isn't too big
    } // Payload isn't too small

    return isValid;
}


/**
 * Checks the length of the payload of a FMTP packet -- as stated in the FMTP
 * header -- against the actual length of a FMTP packet.
 *
 * @param[in] header              The decoded FMTP header.
 * @param[in] nbytes              The size of the FMTP packet in bytes.
 * @throw     std::runtime_error  if the packet is invalid.
 */
void fmtpRecvv3::checkPayloadLen(const FmtpHeader& header, const size_t nbytes)
{
    if (header.payloadlen != nbytes - FMTP_HEADER_LEN) {
        throw std::runtime_error("fmtpRecvv3::checkPayloadLen(): "
                "Invalid payload length");
    }
}


/**
 * Clears the EOP arrival status.
 *
 * @param[in] prodindex    Product index which the EOP belongs to.
 */
void fmtpRecvv3::clearEOPStatus(const uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(EOPmapmtx);
    EOPmap.erase(prodindex);
}


/**
 * Decodes a FMTP packet header. It does the network order to host order
 * translation.
 *
 * @param[in]  packet         The raw packet.
 * @param[out] header         The decoded packet header.
 */
void fmtpRecvv3::decodeHeader(char* const  packet, FmtpHeader& header)
{
    unsigned char* wire = (unsigned char*)packet;
    header.prodindex  = ntohl(*(uint32_t*)wire);
    wire += sizeof(uint32_t);
    header.seqnum     = ntohl(*(uint32_t*)wire);
    wire += sizeof(uint32_t);
    header.payloadlen = ntohs(*(uint16_t*)wire);
    wire += sizeof(uint16_t);
    header.flags      = ntohs(*(uint16_t*)wire);
}


void fmtpRecvv3::updateAckedProd(const uint32_t prodindex)
{
    /**
     * Updates the most recently acknowledged product and notifies a dummy
     * notification handler (getNotify()).
     */
    {
        std::lock_guard<std::mutex> lock(notifyprodmtx);
        notifyprodidx = prodindex;
    }
    notify_cv.notify_one();
}


void fmtpRecvv3::doneWithProd(
        const bool       inTracker,
        struct timespec& now,
        const uint32_t   prodindex,
        const uint32_t   numRetrans)
{
    if (notifier && inTracker) {
        notifier->endProd(now, prodindex, numRetrans);
    }
    else if (inTracker) {
        updateAckedProd(prodindex);
    }

    {
        std::lock_guard<std::mutex> lock(trackermtx);
        trackermap.erase(prodindex);
    }
}


/**
 * Handles a received EOP from the unicast thread. Check the bitmap to see if
 * all the data blocks are received. If true, notify the RecvApp. If false,
 * request for retransmission if it has to be so.
 *
 * @param[in] header           Reference to the received FMTP packet header.
 * @throws std::out_of_range   The notifier doesn't know about
 *                             `header.prodindex`.
 * @throws std::runtime_error  Receiving application error.
 */
void fmtpRecvv3::EOPHandler(const FmtpHeader& header)
{
    /*
     * The time-of-arrival of the end-of-product packets is set as soon as
     * possible in order to be as correct as possible.
     */
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    /**
     * If all data-blocks have been received, then send a
     * RETX_END message back to sender. Meanwhile notify receiving
     * application.
     */
    if (pSegMNG->delIfComplete(header.prodindex)) {
        //sendRetxEnd(header.prodindex);
        msgQueue.push(INLReqMsg{RETX_EOP, header.prodindex, 0, 0});
        bool     inTracker;
        uint32_t numRetrans = -1; // Invalid number
        {
            std::lock_guard<std::mutex> lock(trackermtx);
            inTracker = trackermap.count(header.prodindex);
            if (inTracker)
                numRetrans = trackermap[header.prodindex].numRetrans;
        }
        doneWithProd(inTracker, now, header.prodindex, numRetrans);

        #ifdef MODBASE
            uint32_t tmpidx = header.prodindex % MODBASE;
        #else
            uint32_t tmpidx = header.prodindex;
        #endif

        #ifdef DEBUG2
            std::string debugmsg = "[MSG] Product #" +
                std::to_string(tmpidx);
            debugmsg += " has been completely received";
            std::cout << debugmsg << std::endl;
            WriteToLog(debugmsg);
        #elif DEBUG1
            std::string debugmsg = "[MSG] Product #" +
                std::to_string(tmpidx);
            debugmsg += " has been completely received";
            std::cout << debugmsg << std::endl;
        #endif

        #ifdef MEASURE
            uint32_t bytes = measure->getsize(header.prodindex);
            std::string measuremsg = "[SUCCESS] Product #" +
                std::to_string(tmpidx);
            measuremsg += ": product received, size = ";
            measuremsg += std::to_string(bytes);
            measuremsg += " bytes, elapsed time = ";
            measuremsg += measure->gettime(header.prodindex);
            measuremsg += " seconds.";
            if (measure->getEOPmiss(header.prodindex)) {
                measuremsg += " EOP is retransmitted";
            }
            std::cout << measuremsg << std::endl;
            WriteToLog(measuremsg);
            /* remove the measurement if completely received */
            measure->remove(header.prodindex);
        #endif
    }
    else {
        /**
         * check if the last data block has been received. If true, then
         * all the other missing blocks have been requested. In this case,
         * receiver just needs to wait until product being all completed.
         * Otherwise, last block is missing as well, receiver needs to
         * request retx for all the missing blocks including the last one.
         */
        if (!hasLastBlock(header.prodindex)) {
            uint32_t prodsize;
            bool     haveProdsize;
            {
                std::lock_guard<std::mutex> lock(trackermtx);
                haveProdsize = trackermap.count(header.prodindex) > 0;
                if (haveProdsize)
                    prodsize = trackermap[header.prodindex].prodsize;
            }
            if (haveProdsize)
                requestAnyMissingData(header.prodindex, prodsize);
        }
    }
}


/**
 * Gets the EOP arrival status.
 *
 * @param[in] prodindex    Product index which the EOP belongs to.
 * @retval    `true`       EOP has been received
 * @retval    `false`      EOP has not been received
 */
bool fmtpRecvv3::getEOPStatus(const uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(EOPmapmtx);
    return EOPmap[prodindex];
}


/**
 * Check if the last data block has been received.
 *
 * @param[in] none
 */
bool fmtpRecvv3::hasLastBlock(const uint32_t prodindex)
{
    return pSegMNG->getLastSegment(prodindex);
}


/**
 * Initialize the EOP arrival status.
 *
 * @param[in] prodindex    Product index which the EOP belongs to.
 */
void fmtpRecvv3::initEOPStatus(const uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(EOPmapmtx);
    EOPmap[prodindex] = false;
}


/**
 * Handles a multicast data-packet.
 *   - If the packet is valid:
 *       - If the packet was expected (i.e., the relevant BOP was seen and a
 *         destination exists for the data):
 *           - The data is written to the destination
 *           - Any missing blocks are requested
 *           - The multicast product index is set from the header
 *           - A check is made for an out-of-sequence data-packet
 *       - If the packet wasn't expected:
 *           - Any missing BOP-s are requested
 *   - If the packet is invalid:
 *       - The packet is discarded
 *
 * Executed on the multicast receiving thread.
 *
 * @param[in] header           Peeked-at FMTP header
 * @retval    `true`           The message was valid
 * @retval    `false`          The message was not valid
 * @throw std::runtime_error   Error occurred while reading the socket
 * @throw std::runtime_error   Out-of-sequence data-packet
 */
void fmtpRecvv3::mcastDataHandler(const FmtpHeader& header,
                                  const char*       payload)
{
    uint32_t prodsize = 0;
    void*    prodptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(trackermtx);
        if (trackermap.count(header.prodindex)) {
            ProdTracker tracker = trackermap[header.prodindex];
            prodptr = tracker.prodptr;
            prodsize = tracker.prodsize;
        }
    }

    /**
     * If `prodptr`, then the BOP of the product currently being received
     * was previously received; otherwise, the BOP was either removed or has
     * not yet been received. Since this function is called on the multicast
     * thread, this is likely to be the first time the product is seen. So
     * BOP loss is the only possibility.
     */
    if (prodptr) {
        readMcastData(header, payload, prodptr, prodsize);

        // The data was saved.

        std::lock_guard<std::mutex> lock(antiracemtx);

        requestAnyMissingData(header.prodindex, header.seqnum);

        /* update most recent seqnum and payloadlen */
        {
            std::lock_guard<std::mutex> lock(trackermtx);
            if (trackermap.count(header.prodindex)) {
                trackermap[header.prodindex].seqnum = header.seqnum;
                trackermap[header.prodindex].paylen = header.payloadlen;
            }
        }

        openLeftIndex.setIfHigher(header.prodindex);
    } // Product information exists
    else {
        // Also sets future open left index for BOP requests
        requestBopsIncl(header.prodindex);
    } // Product information doesn't exist

#    ifdef MEASURE
        if (isValid)
            measure->setMcastClock(header.prodindex);
#    endif
}


/**
 * Handles multicast packets.
 *
 * Executed on the multicast receiving thread.
 *
 * @throw std::system_error   if an I/O error occurs.
 * @throw std::runtime_error  Receiving application error.
 * @throw std::out_of_range   The notifier doesn't know about the product-index.
 */
void fmtpRecvv3::mcastHandler()
{
    // Only allow thread cancellation while reading from the socket
    int  cancelState;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelState);

    while(1) {
        FmtpHeader header;
        char*      payload;
        udpRecv.getPacket(header, &payload); // Temporarily enables cancellation

        #if 1
            log_debug("Received via multicast: flags=%#x, prodindex=%s, "
                    "seqnum=%s, payloadlen=%s",
                    header.flags,
                    std::to_string(header.prodindex).data(),
                    std::to_string(header.seqnum).data(),
                    std::to_string(header.payloadlen).data());
        #endif

        if (header.flags == FMTP_BOP) {
            mcastBOPHandler(header, payload);
        }
        else if (header.flags == FMTP_MEM_DATA) {
            mcastDataHandler(header, payload);
        }
        else if (header.flags == FMTP_EOP) {
            mcastEOPHandler(header);
        }
        else {
            log_warning("Ignoring invalid message type: flags=%#x",
                    header.flags);
        }
    } // Indefinite loop
}


/**
 * Handles a received EOP on the multicast thread.
 *
 * @param[in] FmtpHeader       Reference to the received FMTP packet header
 * @throws std::out_of_range   The notifier doesn't know about
 *                             `header.prodindex`.
 * @throws std::runtime_error  Receiving application error.
 * @throws std::runtime_error  Product-index gap is impossibly large
 */
void fmtpRecvv3::mcastEOPHandler(const FmtpHeader& header)
{
    #ifdef MEASURE
        measure->setMcastClock(header.prodindex);
    #endif

    #ifdef MODBASE
        uint32_t tmpidx = header.prodindex % MODBASE;
    #else
        uint32_t tmpidx = header.prodindex;
    #endif

    #ifdef DEBUG2
        std::string debugmsg = "[MCAST EOP] Product #" +
                std::to_string(tmpidx);
        debugmsg += ": EOP is received";
        std::cout << debugmsg << std::endl;
        WriteToLog(debugmsg);
    #endif

    bool hasBOP = false;
    {
        std::lock_guard<std::mutex> lock(trackermtx);
        if (trackermap.count(header.prodindex))
            hasBOP = true;
    }

    if (hasBOP) {
        setEOPStatus(header.prodindex);
        timerWake.notify_all();
        EOPHandler(header);
        openLeftIndex.setIfHigher(header.prodindex);
    }
    else {
        // Also sets future open left index for BOP requests
        requestBopsIncl(header.prodindex);
        #if 0
            /**
             * `lastMcastProd` is only updated if no corresponding BOP is
             * found. Because if we assume packets arriving in sequence,
             * then the index would not change upon EOP arrival. On the
             * other hand, if there do exist out-of-sequence packets,
             * updating `lastMcastProd` could decrease the value. When
             * following packets arrive, the receiver may think a BOP is
             * missed. But if no BOP is found, this EOP is the first packet
             * received with the index, `lastMcastProd` has to be updated to
             * avoid duplicate BOP request since retxBOPHandler does not
             * update `lastMcastProd`. Whether to update lastprodidx after
             * requestMissingBops() returns depends on the return value. A
             * return value of 2 suggests discarding the out-of-sequence
             * packet.
             */
            if (state == 1)
                mcastProdId = header.prodindex;
        #endif
    } // BOP message wasn't received
}


/**
 * Pushes a request for a data-packet onto the retransmission-request queue.
 *
 * @pre                  The retransmission-request queue is locked.
 * @param[in] prodindex  Index of the associated data-product.
 * @param[in] seqnum     Sequence number of the data-packet.
 * @param[in] datalen    Amount of data in bytes.
 */
void fmtpRecvv3::pushDataReq(const uint32_t prodindex,
                                    const uint32_t seqnum,
                                    const uint16_t datalen)
{
    msgQueue.push(INLReqMsg{MISSING_DATA, prodindex, seqnum, datalen});
}


/**
 * Pushes a request for a BOP-packet onto the retransmission-request queue.
 *
 * @param[in] prodindex  Index of the associated data-product.
 */
void fmtpRecvv3::pushBopReq(const uint32_t prodindex)
{
#if !defined(NDEBUG) && defined(LDM_LOGGING)
    log_debug("Pushing BOP request: prodindex=%lu",
            static_cast<unsigned long>(prodindex));
#endif
    msgQueue.push(INLReqMsg{MISSING_BOP, prodindex, 0, 0});
}


/**
 * Pushes a request for a EOP-packet onto the retransmission-request queue.
 *
 * @param[in] prodindex  Index of the associated data-product.
 */
void fmtpRecvv3::pushEopReq(const uint32_t prodindex)
{
    msgQueue.push(INLReqMsg{MISSING_EOP, prodindex, 0, 0});
}


/**
 * Handles all kinds of packets received from the unicast connection. Since
 * the underlying layer offers a stream-based reliable transmission, MSG_PEEK
 * is not necessary any more to reduce extra copies. It's always possible to
 * read the amount of bytes equaling to FMTP_HEADER_LEN first, and read the
 * remaining payload next.
 *
 * @param[in] none
 * @throw std::out_of_range   The notifier doesn't know about the product-index.
 * @throw std::runtime_error  Receiving application error.
 */
void fmtpRecvv3::retxHandler()
{
    FmtpHeader header;
    char       pktHead[FMTP_HEADER_LEN];
    int        initState;
    int        ignoredState;

    (void)memset(pktHead, 0, sizeof(pktHead));
    /*
     * Allow the current thread to be cancelled only when it is likely blocked
     * attempting to read from the unicast socket because that prevents the
     * receiver from being put into an inconsistent state yet allows for fast
     * termination.
     */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &initState);

    while(1)
    {
        /* a useless place holder for header parsing */
        char* pholder;

        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &ignoredState);
        bool success = tcprecv->recvData(pktHead, FMTP_HEADER_LEN, NULL, 0);
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &ignoredState);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        /*
         * recvData returning 0 indicates an unexpected socket close, thus
         * FMTP receiver should stop right away and throw an exception.
         * Since TcpRecv::recvData() either returns 0 or FMTP_HEADER_LEN here,
         * decodeHeader() should only be called if nbytes is not 0. Besides,
         * decodeHeader() itself does not do any header size check, because
         * nbytes should always equal FMTP_HEADER_LEN when successful.
         */
        if (!success) {
            Stop();
#if 1
            throw std::runtime_error("fmtpRecvv3::retxHandler() "
                    "Error reading FMTP header: "
                    "EOF read from retransmission TCP socket.");
#endif
        }
        else {
            /* TcpRecv::recvData() will return requested number of bytes */
            /* This should initialize header: Check Coverity check #3 below.
             * Coverity only complained about header being uninitialized there.
             */
                decodeHeader(pktHead, header);
#if !defined(NDEBUG) && defined(LDM_LOGGING)
                log_debug("Received via unicast: flags=%#x, prodindex=%s,"
                        "seqnum=%s, payloadlen=%s",
                        header.flags,
                        std::to_string(header.prodindex).data(),
                        std::to_string(header.seqnum).data(),
                        std::to_string(header.payloadlen).data());
#endif
        }

        /* dynamically creates a buffer on stack based on payload size */
        const int bufsize = header.payloadlen;
        char      paytmp[bufsize];
        (void)memset(paytmp, 0, bufsize);

        if (header.flags == FMTP_RETX_BOP) {
            (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &ignoredState);
            success = tcprecv->recvData(NULL, 0, paytmp, header.payloadlen);
            (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &ignoredState);

            if (!success) {
                //Stop();
                throw std::runtime_error("fmtpRecvv3::retxHandler() "
                        "Error reading FMTP_RETX_BOP: "
                        "EOF read from the retransmission TCP socket.");
            }
            else if (retxBOPHandler(header, paytmp)) {
                /** remove the BOP from missing list */
                (void)rmMisBOPinSet(header.prodindex);

                {
                    uint32_t                    prodsize;
                    uint32_t                    seqnum;
                    uint32_t                    leftIndex; // Last multicast BOP
                    bool                        inTracker = false;
                    std::lock_guard<std::mutex> lock(antiracemtx);

                    {
                        std::lock_guard<std::mutex> lock(trackermtx);
                        if (trackermap.count(header.prodindex)) {
                            ProdTracker tracker  = trackermap[header.prodindex];
                            prodsize             = tracker.prodsize;
                            seqnum               = tracker.seqnum;
                            leftIndex            = openLeftIndex.get();
                            inTracker            = true;
                        }
                    }
                    if (inTracker) {
                        /**
                         * If seqnum != 0, the seqnum is updated right after the
                         * retx BOP is handled, which means the multicast thread
                         * is receiving blocks. In this case, nothing needs to
                         * be done.
                         */
                        if (seqnum == 0) {
                            /**
                             * If two indices don't equal, the product is
                             * totally missed. Thus, all blocks should be
                             * requested. On the other hand, if they equal,
                             * there could be concurrency or a gap before next
                             * product arrives. Only requesting EOP is the most
                             * economic choice.
                             */
                            if (leftIndex != header.prodindex) {
                                requestAnyMissingData(header.prodindex, prodsize);
                            }
                            pushEopReq(header.prodindex);
                        }
                    } // Product has entry in tracker-map
                    else {
                        # if LDM_LOGGING
                            log_notice("fmtpRecvv3::retxHandler() Product not "
                                    "found in trackermap after retx BOP. "
                                    "Erased by another thread?");
                        # endif
                    } // Product doesn't have entry in tracker-map
                } // Anti-race mutex is locked
            } // BOP message is valid
        } // Message is BOP retransmission
        else if (header.flags == FMTP_RETX_DATA) {
            #ifdef MEASURE
                /* log the time first */
                measure->setRetxClock(header.prodindex);
            #endif

            #ifdef MODBASE
                uint32_t tmpidx = header.prodindex % MODBASE;
            #else
                uint32_t tmpidx = header.prodindex;
            #endif

            #ifdef DEBUG2
                std::string debugmsg = "[RETX DATA] Product #" +
                    std::to_string(tmpidx);
                debugmsg += ": Data block received on unicast, SeqNum = ";
                debugmsg += std::to_string(header.seqnum);
                debugmsg += ", Paylen = ";
                debugmsg += std::to_string(header.payloadlen);
                std::cout << debugmsg << std::endl;
                WriteToLog(debugmsg);
            #endif

            uint32_t prodsize = 0;
            void*    prodptr  = NULL;
            {
                std::lock_guard<std::mutex> lock(trackermtx);
                if (trackermap.count(header.prodindex)) {
                    ProdTracker& tracker = trackermap[header.prodindex];
                    prodsize = tracker.prodsize;
                    prodptr  = tracker.prodptr;
                    ++tracker.numRetrans;
                }
            }

            if ((prodsize > 0) &&
                (header.seqnum + header.payloadlen > prodsize)) {
                throw std::runtime_error("fmtpRecvv3::retxHandler() "
                        "retx block out of boundary: seqnum=" +
                        std::to_string(header.seqnum) + ", payloadlen=" +
                        std::to_string(header.payloadlen) + "prodsize=" +
                        std::to_string(prodsize));
            }
            else if (prodsize <= 0) {
                (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,
                                             &ignoredState);
                /**
                 * drop the payload since there is no location allocated
                 * in the product queue.
                 */
                success = tcprecv->recvData(NULL, 0, paytmp, header.payloadlen);
                (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,
                                             &ignoredState);
                if (!success) {
                    throw std::runtime_error("fmtpRecvv3::retxHandler() "
                            "Error reading FMTP_RETX_DATA (prodsize <= 0): "
                            "EOF read from the retransmission TCP socket.");
                }

                /*
                 * The TrackerMap will only be erased when the associated
                 * product has been completely received. So if no valid
                 * prodindex found, it indicates the product is received
                 * and thus removed or there is out-of-order arrival on
                 * TCP.
                 */
                continue;
            }

            if (prodptr) {
                (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,
                                             &ignoredState);
                success = tcprecv->recvData(NULL, 0, (char*)prodptr +
                                           header.seqnum, header.payloadlen);
                (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,
                                             &ignoredState);
                if (!success) {
                    throw std::runtime_error("fmtpRecvv3::retxHandler() "
                            "Error reading FMTP_RETX_DATA (with prodptr): "
                            "EOF read from the retransmission TCP socket.");
                }
            }
            else {
                /** dump the payload since there is no product queue */
                (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,
                                             &ignoredState);
                success = tcprecv->recvData(NULL, 0, paytmp, header.payloadlen);
                (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,
                                             &ignoredState);
                if (!success) {
                    throw std::runtime_error("fmtpRecvv3::retxHandler() "
                            "Error reading FMTP_RETX_DATA (without prodptr): "
                            "EOF read from the retransmission TCP socket.");
                }
            }

            /**
             * set() returns -1/0/1, receiver can parse the info for detailed
             * operations. But currently it is ignored to keep the process going
             */
            pSegMNG->set(header.prodindex, header.seqnum, header.payloadlen);

            if (pSegMNG->delIfComplete(header.prodindex)) {
                //sendRetxEnd(header.prodindex);
                msgQueue.push(INLReqMsg{RETX_EOP, header.prodindex, 0, 0});
                bool     inTracker;
                uint32_t numRetrans = -1; // Invalid number
                {
                    std::lock_guard<std::mutex> lock(trackermtx);
                    inTracker = trackermap.count(header.prodindex);
                    if (inTracker)
                        numRetrans = trackermap[header.prodindex].numRetrans;
                }
                doneWithProd(inTracker, now, header.prodindex, numRetrans);

                #ifdef DEBUG2
                    std::string debugmsg = "[MSG] Product #" +
                        std::to_string(tmpidx);
                    debugmsg += " has been completely received";
                    std::cout << debugmsg << std::endl;
                    WriteToLog(debugmsg);
                #elif DEBUG1
                    std::string debugmsg = "[MSG] Product #" +
                        std::to_string(tmpidx);
                    debugmsg += " has been completely received";
                    std::cout << debugmsg << std::endl;
                #endif

                #ifdef MEASURE
                    uint32_t bytes = measure->getsize(header.prodindex);
                    std::string measuremsg = "[SUCCESS] Product #" +
                        std::to_string(tmpidx);
                    measuremsg += ": product received, size = ";
                    measuremsg += std::to_string(bytes);
                    measuremsg += " bytes, elapsed time = ";
                    measuremsg += measure->gettime(header.prodindex);
                    measuremsg += " seconds.";
                    if (measure->getEOPmiss(header.prodindex)) {
                        measuremsg += " EOP is retransmitted";
                    }
                    std::cout << measuremsg << std::endl;
                    WriteToLog(measuremsg);
                    /* remove the measurement if completely received */
                    measure->remove(header.prodindex);
                #endif
            }
        }
        else if (header.flags == FMTP_RETX_EOP) {
            #ifdef MEASURE
                measure->setRetxClock(header.prodindex);
                measure->setEOPmiss(header.prodindex);
            #endif
            /*
             * Coverity Scan #1: Issue 3: Priority supposedly high, claims header is uninitialized.
             * Header should be initialized in the decodeHeader function called above. Ignore for now..
             * 8/3/2016 - Ryan Aubrey
             */
            retxEOPHandler(header);
        }
        else if (header.flags == FMTP_RETX_REJ) {
            const bool hadBop = rmMisBOPinSet(header.prodindex);
            /*
             * if associated segmap exists, remove the segmap. Also avoid
             * duplicated notification if the product's segmap has
             * already been removed.
             */
            if (pSegMNG->rmProd(header.prodindex) || hadBop) {
                #ifdef MODBASE
                    uint32_t tmpidx = header.prodindex % MODBASE;
                #else
                    uint32_t tmpidx = header.prodindex;
                #endif

                #ifdef DEBUG2
                    std::string debugmsg = "[FAILURE] Product #" +
                        std::to_string(tmpidx);
                    debugmsg += " is not completely received";
                    std::cout << debugmsg << std::endl;
                    WriteToLog(debugmsg);
                #endif

                if (notifier) {
                    notifier->missedProd(header.prodindex);
                }
                else {
                    updateAckedProd(header.prodindex);
                }

                {
                    std::lock_guard<std::mutex> lock(trackermtx);
                    trackermap.erase(header.prodindex);
                }
            }
        }
    }

    (void)pthread_setcancelstate(initState, &ignoredState);
}


/**
 * Fetch the requests from an internal message queue and call corresponding
 * handler to send requests respectively. The read operation on the internal
 * message queue will block if the queue is empty itself. The existing request
 * being handled will only be removed from the queue if the handler returns a
 * successful state. Doesn't return until a "shutdown" request is encountered or
 * an error occurs.
 *
 * @param[in] none
 */
void fmtpRecvv3::retxRequester()
{
    while(1)
    {
        const INLReqMsg& reqmsg = msgQueue.front();

        if (reqmsg.reqtype == SHUTDOWN)
            break; // leave "shutdown" message in queue

        bool success = false;

        if (reqmsg.reqtype == MISSING_BOP) {
            success = sendBOPRetxReq(reqmsg.prodindex);
        }
        else if (reqmsg.reqtype == MISSING_DATA) {
            success = sendDataRetxReq(reqmsg.prodindex, reqmsg.seqnum,
                    reqmsg.payloadlen);
        }
        else if (reqmsg.reqtype == MISSING_EOP) {
            success = sendEOPRetxReq(reqmsg.prodindex);
        }
        else if (reqmsg.reqtype == RETX_EOP) {
                success = sendRetxEnd(reqmsg.prodindex);
        }

        if (success)
            msgQueue.pop();
    }
}


/**
 * Remove the BOP identified by the given prodindex out of the list. If the
 * BOP is not in the list, return a false. Or if it's in the list, remove it
 * and reurn a true.
 *
 * @param[in] prodindex        Product index of the missing BOP
 */
bool fmtpRecvv3::rmMisBOPinSet(uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(BOPSetMtx);
    bool rmsuccess = misBOPset.erase(prodindex);
    return rmsuccess;
}


/**
 * Handles a received EOP from the unicast thread. No need to remove the data,
 * just call the handling process directly.
 *
 * @param[in] FmtpHeader    Reference to the received FMTP packet header
 * @throw std::out_of_range   The notifier doesn't know about
 *                            `header.prodindex`.
 * @throw std::runtime_error  Receiving application error.
 */
void fmtpRecvv3::retxEOPHandler(const FmtpHeader& header)
{
    #ifdef MODBASE
        uint32_t tmpidx = header.prodindex % MODBASE;
    #else
        uint32_t tmpidx = header.prodindex;
    #endif

    #ifdef DEBUG2
        std::string debugmsg = "[RETX EOP] Product #" +
            std::to_string(tmpidx);
        debugmsg += ": EOP is received";
        std::cout << debugmsg << std::endl;
        WriteToLog(debugmsg);
    #endif

    bool hasBOP = false;
    {
        std::lock_guard<std::mutex> lock(trackermtx);
        if (trackermap.count(header.prodindex)) {
            hasBOP = true;
        }
    }
    if (hasBOP) {
        EOPHandler(header);
    }
    else {
        /**
         * TODO: handle forced EOP
         * If we see a RETX_EOP with no BOP received before, it is
         * a forced EOP to avoid the silent loss of the last file
         * in a file-stream. A straight forward way to handle this
         * case is to call notify_of_missed_prod() and then increment
         * `lastMcastProd` so next time a new file comes in, this file
         * will not be requested again. However, `lastMcastProd` can
         * only be accessed by multicast thread, updating it here
         * could mess up the sequence of files on multicast.
         * Also, what is the end of a file-stream. If a file arrives
         * 30 min after the previous file (considered as end?), should
         * we force an EOP for the previous file? I think this handling
         * needs to be discussed in detail. Currently, the best way is
         * to simply ignore the forced EOP.
         */
    }
}


/**
 * Reads the data portion of a FMTP data-packet into the location specified
 * by the receiving application given the associated, peeked-at, and decoded
 * FMTP header. Discards the packet if the FMTP message is invalid.
 *
 * @pre                       The socket contains a FMTP data-packet.
 * @param[in] header          The associated, peeked-at, and decoded header.
 * @param[in] payload         `header.payloadlen` bytes of payload
 * @param[in] prodptr         Destination for data
 * @param[in] prodsize        Size of data in bytes
 * @retval    `true`          FMTP message was valid and data was saved
 * @retval    `false`         FMTP message wasn't valid and data was discarded
 * @throw std::system_error   if an error occurs while reading the multicast
 *                            socket.
 */
void fmtpRecvv3::readMcastData(const FmtpHeader& header,
                               const char*       payload,
                               void* const       prodptr,
                               const uint32_t    prodsize)
{
    assert(prodptr);

    if (header.seqnum + header.payloadlen > prodsize) {
        #if !defined(NDEBUG) && defined(LDM_LOGGING)
            log_warning("Data segment extends beyond product. Discarding.");
        #endif
    }
    else {
        ::memcpy(static_cast<char*>(prodptr)+header.seqnum, payload,
                header.payloadlen);

        #ifdef MODBASE
            uint32_t tmpidx = header.prodindex % MODBASE;
        #else
            uint32_t tmpidx = header.prodindex;
        #endif

        #ifdef DEBUG2
            std::string debugmsg = "[MCAST DATA] Product #" +
                    std::to_string(tmpidx);
            debugmsg += ": Data block received from multicast. SeqNum = ";
            debugmsg += std::to_string(header.seqnum);
            debugmsg += ", Paylen = ";
            debugmsg += std::to_string(header.payloadlen);
            std::cout << debugmsg << std::endl;
            WriteToLog(debugmsg);
        #endif

        /**
         * Since now receiver has no knowledge about the segment size, it
         * trusts the packet from sender is legal. Also, the product
         * segment-manager has control to make sure no malicious segments
         * will be ACKed.
         */
        pSegMNG->set(header.prodindex, header.seqnum, header.payloadlen);
    } // Data-segment size is not too large and payload length is correct
}


/**
 * Requests data-packets that lie between the last previously-received
 * data-packet of the current data-product and its most recently-received
 * data-packet.
 *
 * @pre                  The most recently-received data-packet is for the
 *                       current data-product.
 * @param[in] prodindex  Index of current product.
 * @param[in] mostRecent Sequence number of most recently-received data-packet
 *                       of current data-product.
 */
void fmtpRecvv3::requestAnyMissingData(const uint32_t prodindex,
                                       const uint32_t mostRecent)
{
    uint32_t seqnum = 0;
    {
        std::lock_guard<std::mutex> lock(trackermtx);
        if (trackermap.count(prodindex)) {
            ProdTracker tracker = trackermap[prodindex];
            seqnum = tracker.seqnum + tracker.paylen;
        }
    }

    /**
     * requests for missing blocks counting from the last received
     * block sequence number.
     */
    if (seqnum != mostRecent) {
#       ifdef LDM_LOGGING
            if (mostRecent < seqnum)
                log_warning("Unexpected sequence number: product=%" PRIu32
                        ", expected=%" PRIu32 ", actual=%" PRIu32,
                        prodindex, seqnum, mostRecent);
#       endif

        // TODO: Merged RETX_REQ cannot be implemented so far, because
        // current FMTP header has a limited payloadlen field of 16 bits.
        // A merged RETX_REQ could have more than 65535 in payloadlen, the
        // field thus needs to be upgraded to 32 bits. We will do this in
        // the next version of FMTP.
        //
        /* merged requests, multiple missing blocks in one request */
        // pushMissingDataReq(prodindex, seqnum, mostRecent - seqnum);

        for (; seqnum < mostRecent; seqnum += fmtpBase.MAX_PAYLOAD) {
            pushDataReq(prodindex, seqnum, fmtpBase.MAX_PAYLOAD);

            #ifdef MODBASE
                uint32_t tmpidx = prodindex % MODBASE;
            #else
                uint32_t tmpidx = prodindex;
            #endif

            #ifdef DEBUG2
                std::string debugmsg = "[RETX REQ] Product #" +
                    std::to_string(tmpidx);
                debugmsg += ": Data block is missing. SeqNum = ";
                debugmsg += std::to_string(seqnum);
                debugmsg += ", PayLen = ";
                debugmsg += std::to_string(mostRecent - seqnum);
                debugmsg += ". Request retx.";
                std::cout << debugmsg << std::endl;
                WriteToLog(debugmsg);
            #endif
        }
    }
}


/**
 * Requests BOP packets for a positive product-index gap.
 *
 * @param[in] openleft            Open left end of the product-index interval.
 * @param[in] openright           Open right end of the product-index interval.
 * @throws    std::runtime_error  Product-index gap is impossibly large
 */
void fmtpRecvv3::requestBops(const uint32_t openleft,
                             const uint32_t openright)
{
    const uint32_t delta = openright - openleft;

    if (delta) {
        if (delta > UINT32_MAX/2) {
#           if LDM_LOGGING
                log_warning("Invalid product gap: openleft=" PRIu32
                        ", openright=" PRIu32, openleft, openright);
#           endif
        }
        else {
            for (uint32_t i = (openleft + 1); i != openright; ++i)
                if (addUnrqBOPinSet(i))
                    pushBopReq(i);
        }
    }
}

/**
 * Requests BOP packets for data-products that come after the data-product
 * returned by `openLeftIndex.setIfHigher()` and up to but excluding a given
 * data-product.
 *
 * Called when a BOP message for the previous multicast product hasn't been
 * received.
 *
 * Calls `openLeftIndex.setIfHigher()` with the given index.
 *
 * Executed on the multicast receiving thread.
 *
 * @param[in] prodindex           Product-index to be excluded and given to
 *                                `openLeftIndex.setIfHigher()`
 * @throws    std::runtime_error  Product-index gap is impossibly large
 */
void fmtpRecvv3::requestBopsExcl(const uint32_t prodindex)
{
    const uint32_t leftIndex = openLeftIndex.setIfHigher(prodindex);

    requestBops(leftIndex, prodindex);
}


/**
 * Requests BOP packets for data-products that come after the data-product
 * returned by `openLeftIndex.setIfHigher()` and up to and including a given
 * data-product.
 *
 * Called when a BOP message for the current multicast product hasn't been
 * received.
 *
 * Calls `openLeftIndex.setIfHigher()` with the given index.
 *
 * Executed on the multicast receiving thread.
 *
 * @param[in] prodindex           Product-index to be include and given to
 *                                `openLeftIndex.setIfHigher()`
 */
void fmtpRecvv3::requestBopsIncl(const uint32_t prodindex)
{
    const uint32_t leftIndex = openLeftIndex.setIfHigher(prodindex);

    requestBops(leftIndex, prodindex+1);
}


/**
 * Request for EOP retransmission if the EOP is not received. This function is
 * an integration of isEOPReceived() and pushMissingEopReq() but being made
 * atomic. If the EOP is requested by this function, it returns a boolean true.
 * Otherwise, if not requested, it returns a boolean false.
 *
 * @param[in] prodindex        Product index which the EOP is using.
 */
bool fmtpRecvv3::reqEOPifMiss(const uint32_t prodindex)
{
    bool hasReq = false;
    if (!getEOPStatus(prodindex)) {
        pushEopReq(prodindex);
        hasReq = true;
    }
    return hasReq;
}


/**
 * Start the actual timer thread.
 *
 * @param[in] *ptr    A pointer to an fmtpRecvv3 instance.
 */
void* fmtpRecvv3::runTimerThread(void* ptr)
{
    fmtpRecvv3* const recvr = static_cast<fmtpRecvv3*>(ptr);
    try {
        recvr->timerThread();
    }
    catch (std::runtime_error& e) {
        recvr->taskExit(std::current_exception());
    }
    return NULL;
}


/**
 * Sends a request for retransmission of the missing BOP identified by the
 * given product index.
 *
 * @param[in] prodindex        The product index of the requested BOP.
 */
bool fmtpRecvv3::sendBOPRetxReq(uint32_t prodindex)
{
    FmtpHeader header;
    header.prodindex  = htonl(prodindex);
    header.seqnum     = 0;
    header.payloadlen = 0;
    header.flags      = htons(FMTP_BOP_REQ);

    #if !defined(NDEBUG) && defined(LDM_LOGGING)
        log_debug("Sending BOP-retransmission-request");
    #endif

    return (-1 != tcprecv->send(header));
}


/**
 * Sends a request for retransmission of the missing EOP identified by the
 * given product index.
 *
 * @param[in] prodindex        The product index of the requested EOP.
 */
bool fmtpRecvv3::sendEOPRetxReq(uint32_t prodindex)
{
    FmtpHeader header;
    header.prodindex  = htonl(prodindex);
    header.seqnum     = 0;
    header.payloadlen = 0;
    header.flags      = htons(FMTP_EOP_REQ);

    #if !defined(NDEBUG) && defined(LDM_LOGGING)
        log_debug("Sending EOP-retransmission-request");
    #endif
    return (-1 != tcprecv->send(header));
}


/**
 * Sends a request for retransmission of the missing block. The sequence
 * number and payload length are guaranteed to be aligned to the boundary of
 * a legal block.
 *
 * @param[in] prodindex        The product index of the requested block.
 * @param[in] seqnum           The sequence number of the requested block.
 * @param[in] payloadlen       The block size of the requested block.
 */
bool fmtpRecvv3::sendDataRetxReq(uint32_t prodindex, uint32_t seqnum,
                                  uint16_t payloadlen)
{
    FmtpHeader header;
    header.prodindex  = htonl(prodindex);
    header.seqnum     = htonl(seqnum);
    header.payloadlen = htons(payloadlen);
    header.flags      = htons(FMTP_RETX_REQ);

    #if !defined(NDEBUG) && defined(LDM_LOGGING)
        log_debug("Sending data-retransmission-request");
    #endif
    return (-1 != tcprecv->send(header));
}

/**
 * Sends a retransmission end message to the sender to indicate the product
 * indexed by prodindex has been completely received.
 *
 * @param[in] prodindex        The product index of the finished product.
 * @retval    `true`           Success
 * @retval    `false`          Failure
 */
bool fmtpRecvv3::sendRetxEnd(const uint32_t prodindex) const
{
    FmtpHeader header;
    header.prodindex  = htonl(prodindex);
    header.seqnum     = 0;
    header.payloadlen = 0;
    header.flags      = htons(FMTP_RETX_END);

    #if !defined(NDEBUG) && defined(LDM_LOGGING)
        log_debug("Sending retransmission-end");
    #endif

    return (-1 != tcprecv->send(header));
}


/**
 * Start the retxRequester thread using a passed-in fmtpRecvv3 pointer. Called
 * by `pthread_create()`.
 *
 * @param[in] *ptr        A pointer to the pre-defined data structure in the
 *                        caller. Here it's the pointer to a fmtpRecvv3
 *                        instance.
 */
void* fmtpRecvv3::StartRetxRequester(void* ptr)
{
    fmtpRecvv3* const recvr = static_cast<fmtpRecvv3*>(ptr);
    try {
        recvr->retxRequester();
    }
    catch (std::runtime_error& e) {
        recvr->taskExit(std::current_exception());
    }
    return NULL;
}


/**
 * Stops the retransmission-request task by adding a "shutdown" request to the
 * associated queue and joins with its thread.
 *
 * @throws std::runtime_error If the retransmission-request thread can't be
 *                            joined.
 */
void fmtpRecvv3::stopJoinRetxRequester()
{
    msgQueue.push(INLReqMsg{SHUTDOWN});

    int status = pthread_join(retx_rq, NULL);
    if (status) {
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::stopJoinRetxRequester() Couldn't "
                "join retransmission-request thread");
    }
}


/**
 * Start the retxHandler thread using a passed-in fmtpRecvv3 pointer. Called
 * by `pthread_create()`.
 *
 * @param[in] *ptr        A pointer to the pre-defined data structure in the
 *                        caller. Here it's the pointer to a fmtpRecvv3 class.
 */
void* fmtpRecvv3::StartRetxHandler(void* ptr)
{
    fmtpRecvv3* const recvr = static_cast<fmtpRecvv3*>(ptr);
    try {
        recvr->retxHandler();
    }
    catch (std::exception& e) {
        recvr->taskExit(std::current_exception());
    }
    return NULL;
}


/**
 * Stops the retransmission-reception task by canceling its thread and joining
 * it.
 *
 * @throws std::runtime_error If the retransmission-reception thread can't be
 *                            canceled.
 * @throws std::runtime_error If the retransmission-reception thread can't be
 *                            joined.
 */
void fmtpRecvv3::stopJoinRetxHandler()
{
    if (!retxHandlerCanceled.test_and_set()) {
        int status = pthread_cancel(retx_t);
        if (status && status != ESRCH) {
            throw std::system_error(errno, std::system_category(),
                    "fmtpRecvv3::stopJoinMcastHandler() "
                    "Couldn't cancel retransmission-reception thread");
        }
        status = pthread_join(retx_t, NULL);
        if (status && status != ESRCH) {
            throw std::system_error(errno, std::system_category(),
                    "fmtpRecvv3::stopJoinRetxHandler() "
                    "Couldn't join retransmission-reception thread");
        }
    }
}


/**
 * Starts the multicast-receiving task of a FMTP receiver. Called by
 * `::pthread_create()`.
 *
 * @param[in] arg   Pointer to the FMTP receiver.
 * @retval    NULL  Always.
 */
void* fmtpRecvv3::StartMcastHandler(
        void* const arg)
{
    fmtpRecvv3* const recvr = static_cast<fmtpRecvv3*>(arg);

    try {
        recvr->mcastHandler();
    }
    catch (const std::exception& e) {
        #ifdef LDM_LOGGING
    		log_add("mcastHandler() failure");
    		log_flush_error();
        #endif
        recvr->taskExit(std::current_exception());
    }

    return NULL;
}


/**
 * Stops the muticast task by canceling its thread and joining it.
 *
 * @throws std::runtime_error if the multicast thread can't be canceled.
 * @throws std::runtime_error if the multicast thread can't be joined.
 */
void fmtpRecvv3::stopJoinMcastHandler()
{
    if (!mcastHandlerCanceled.test_and_set()) {
        int status = pthread_cancel(mcast_t);
        if (status && status != ESRCH) {
            throw std::system_error(errno, std::system_category(),
                    "fmtpRecvv3::stopJoinMcastHandler() "
                    "Couldn't cancel multicast thread");
        }
        status = pthread_join(mcast_t, NULL);
        if (status && status != ESRCH) {
            throw std::system_error(errno, std::system_category(),
                    "fmtpRecvv3::stopJoinMcastHandler() "
                    "Couldn't join multicast thread");
        }
    }
}


/**
 * Start a Retx procedure, including the retxHandler thread and retxRequester
 * thread. These two threads will be started independently and after the
 * procedure returns, it continues to run the mcastHandler thread.
 *
 * @throws    std::runtime_error If a retransmission-reception thread can't be
 *                               started.
 * @throws    std::runtime_error If a retransmission-request thread can't be
 *                               started.
 */
void fmtpRecvv3::StartRetxProcedure()
{
    int retval = pthread_create(&retx_t, NULL, &fmtpRecvv3::StartRetxHandler,
                                this);
    if(retval != 0) {
        throw std::system_error(errno, std::system_category(),
            "fmtpRecvv3::StartRetxProcedure() pthread_create() error");
    }

    retval = pthread_create(&retx_rq, NULL, &fmtpRecvv3::StartRetxRequester,
                            this);
    if(retval != 0) {
        try {
            stopJoinRetxHandler();
        }
        catch (...) {}
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::StartRetxProcedure() "
                "pthread_create() failed with retval = "
                + std::to_string(retval));
    }
}


/**
 * Starts a timer thread to watch for the case of missing EOP.
 *
 * @return  none
 */
void fmtpRecvv3::startTimerThread()
{
    int retval = pthread_create(&timer_t, NULL, &fmtpRecvv3::runTimerThread,
            this);

    if(retval != 0) {
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::startTimerThread() "
                "pthread_create() failed with retval = " + std::to_string(retval));
    }
}


/**
 * Stops the timer task by adding a "shutdown" entry to the associated queue and
 * joins with its thread.
 *
 * @throws std::runtime_error if the timer thread can't be joined.
 */
void fmtpRecvv3::stopJoinTimerThread()
{
    timerParam timerparam;
    {
        std::lock_guard<std::mutex> lock(timerQmtx);
        timerParam timerparam = {0, -1.0};
        timerParamQ.push(timerparam);
        timerQfilled.notify_one();
    }

    int status = pthread_join(timer_t, NULL);
    if (status) {
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::stopJoinTimerThread() "
                "Couldn't join timer thread");
    }
}


/**
 * Sets the EOP arrival status to true, which indicates the successful
 * reception of EOP.
 *
 * @param[in] prodindex    Product index which the EOP belongs to.
 */
void fmtpRecvv3::setEOPStatus(const uint32_t prodindex)
{
    std::lock_guard<std::mutex> lock(EOPmapmtx);
    EOPmap[prodindex] = true;
}


/**
 * Runs a timer thread to watch for the case of missing EOP. If an expected
 * EOP is not received, the timer should trigger after sleeping. If it is
 * received from the mcast socket, do not trigger to request for retransmission
 * of the EOP. mcastEOPHandler will notify the condition variable to interrupt
 * the timed wait function. Doesn't return unless a "shutdown" entry is
 * encountered or an exception is thrown.
 */
void fmtpRecvv3::timerThread()
{
    while (1) {
        timerParam timerparam;
        {
            std::unique_lock<std::mutex> lock(timerQmtx);
            while (timerParamQ.empty())
                timerQfilled.wait(lock);
            timerparam = timerParamQ.front();
        }

        if (timerparam.seconds < 0)
            break; // leave "shutdown" entry in queue

        unsigned long period = timerparam.seconds * 1000000000lu;
        {
            std::unique_lock<std::mutex> lk(timerWakemtx);
            /** sleep for a given amount of time in precision of nanoseconds */
            timerWake.wait_for(lk, std::chrono::nanoseconds(period));
        }

        /** pop the current entry in timer queue when timer wakes up */
        {
            std::lock_guard<std::mutex> lock(timerQmtx);
            timerParamQ.pop();
        }

        /** if EOP has not been received yet, issue a request for retx */
        if (reqEOPifMiss(timerparam.prodindex)) {
            #ifdef MODBASE
                uint32_t tmpidx = timerparam.prodindex % MODBASE;
            #else
                uint32_t tmpidx = timerparam.prodindex;
            #endif

            #ifdef DEBUG2
            std::string debugmsg = "[TIMER] Timer has waken up. Product #" +
                    std::to_string(tmpidx);
                debugmsg += " is still missing EOP. Request retx.";
                std::cout << debugmsg << std::endl;
                WriteToLog(debugmsg);
            #endif
        }
        /**
         * After waking up, the timer checks the EOP arrival status of
         * a product and decides whether to request for re-transmission.
         * Only the timer can clear the EOPmap.
         */
        clearEOPStatus(timerparam.prodindex);
    }
}


/**
 * Task terminator. If an exception is thrown by an independent task executing
 * on an independent thread, then this function will be called. It consequently
 * terminates all the other tasks. Blocks on the exit mutex; otherwise, returns
 * immediately.
 *
 * @param[in] e                  Exception pointer
 */
void fmtpRecvv3::taskExit(const std::exception_ptr& e)
{
    {
        std::lock_guard<std::mutex> lock(exitMutex);
        if (!except) {
            /**
             * make_exception_ptr() will make a copy of the exception of the
             * declared type, which is std::runtime_error, instead of what it is
             * actually. Since the passed-in exception can be any derived
             * exception type, this would cause slicing and losing information.
             */
            except = e;
        }
        exitCond.notify_one();
    }
}


/**
 * Write a line of log record into the log file. If the log file doesn't exist,
 * create a new one and then append to it.
 *
 * @param[in] content       The content of the log record to be written.
 */
void fmtpRecvv3::WriteToLog(const std::string& content)
{
    time_t      rawtime;
    struct tm*  timeinfo;
    char        buf[30];
    /* create logs  directory if it doesn't exist */
    int         status = mkdir("logs", 0755);

    if (status && status != EEXIST)
        throw std::system_error(errno, std::system_category(),
                "fmtpRecvv3::WriteToLog(): unable to create "
                "logs directory. This could be a permissions issue.");

    /* allocate a large enough buffer in case some long hostnames */
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string logpath(hostname);
    logpath = "logs/FMTPv3_RECEIVER_" + logpath + ".log";

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buf, 30, "%Y-%m-%d %I:%M:%S  ", timeinfo);
    std::string time(buf);

    std::ofstream logfile(logpath, std::ofstream::out | std::ofstream::app);
    logfile << time << content << std::endl;
    logfile.close();
}

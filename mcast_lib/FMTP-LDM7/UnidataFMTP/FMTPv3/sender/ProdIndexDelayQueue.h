/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ProdIndexDelayQueue.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API of a thread-safe delay-queue of product-indexes.
 */

#ifndef FMTP_SENDER_PRODINDEXDELAYQUEUE_H_
#define FMTP_SENDER_PRODINDEXDELAYQUEUE_H_


#include <sys/types.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <vector>


class ProdIndexDelayQueue {
public:
    /**
     * Constructs an instance.
     *
     * **Exception Safety:** Strong guarantee
     *
     * @throws std::bad_alloc     If necessary memory can't be allocated.
     * @throws std::system_error  If a system error occurs.
     */
    ProdIndexDelayQueue();
    /**
     * Adds an element to the queue.
     *
     * **Exception Safety:** Strong guarantee
     *
     * @param[in] index    The product-index.
     * @param[in] seconds  The duration, in seconds, to the reveal-time of the
     *                     product-index (i.e., until the element can be
     *                     retrieved via `pop()`).
     */
    void push(uint32_t index, double seconds);
    /**
     * Returns the product-index whose reveal-time is the earliest and not later
     * than the current time and removes it from the queue. Blocks until such a
     * product-index exists.
     *
     * **Exception Safety:** Basic guarantee
     *
     * @return  The product-index with the earliest reveal-time that's not later
     *          than the current time.
     */
    uint32_t pop();
    /**
     * Unconditionally returns the product-index whose reveal-time is the
     * earliest and removes it from the queue. Undefined behavior results if the
     * queue is empty.
     *
     * **Exception Safety:** Basic guarantee
     *
     * @return  The product-index with the earliest reveal-time.
     */
    uint32_t get();
    /**
     * Returns the number of product-indexes in the queue.
     *
     * @return  The number of product-indexes in the queue.
     */
    size_t size() noexcept;
    /**
     * Disables the queue. After this call, both `push()` and `pop` will fail.
     */
    void disable() noexcept;

private:
    /**
     * An element in the priority-queue of a `ProdIndexDelayQueue` instance.
     */
    class Element {
    public:
        /**
         * Constructs an instance.
         *
         * @param[in] index    The product-index.
         * @param[in] seconds  The duration, in seconds, from the current time
         *                     until the reveal-time of the element. May be
         *                     negative.
         */
        Element(uint32_t index, double seconds);
        /**
         * Constructs an instance.
         *
         * @param[in] index    The product-index.
         * @param[in] seconds  The duration, in seconds, from the current time
         *                     until the reveal-time of the element. May be
         *                     negative.
         */
        bool      isLaterThan(const Element& that) const;
        /**
         * Returns the product-index.
         *
         * @return  The product-index.
         */
        uint32_t getIndex() const noexcept {return index;}
        /**
         * Returns the reveal-time.
         *
         * @return  The reveal-time.
         */
        const std::chrono::system_clock::time_point&
                  getTime() const {return when;}
    private:
        /**
         * The product-index.
         */
        uint32_t                             index;
        /**
         * The reveal-time.
         */
        std::chrono::system_clock::time_point when;
    };

    /**
     * Indicates if the priority of an element is lower than the priority of
     * another element.
     *
     * @param[in] a     The first element.
     * @param[in] b     The second element.
     * @retval    true  If and only if the priority of the first element is less
     *                  than the priority of the second element.
     */
    static bool
            isLowerPriority(const ProdIndexDelayQueue::Element& a,
                const ProdIndexDelayQueue::Element& b);
    /**
     * Returns the time associated with the highest-priority element in the
     * queue.
     *
     * @pre        The instance is locked.
     * @param[in]  The lock on the instance.
     * @return     The time at which the earliest element will be ready.
     */
    const std::chrono::system_clock::time_point&
            getEarliestTime(std::unique_lock<std::mutex>& lock);
    /**
     * Throws the appropriate exception if the queue is disabled.
     *
     * @pre                        The queue is locked.
     * @throws std::runtime_error  if the queue is disabled.
     */
    void throwIfDisabled() const {
        if (disabled)
            throw std::runtime_error("Product-index delay-queue is disabled");
    }

    /**
     * The mutex for protecting the priority-queue.
     */
    std::mutex                          mutex;
    /**
     * The condition variable for signaling when the priority-queue has been
     * modified.
     */
    std::condition_variable             cond;
    /**
     * The priority-queue.
     */
    std::priority_queue<Element, std::vector<Element>,
            decltype(&isLowerPriority)> priQ;
    /**
     * Whether or not the queue is disabled.
     */
    bool                                disabled;
};

#endif /* FMTP_SENDER_PRODINDEXDELAYQUEUE_H_ */

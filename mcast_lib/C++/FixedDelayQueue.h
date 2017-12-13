/**
 * This file declares a thread-safe, fixed-duration, delay-queue.
 *
 * Copyright 2017 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYING in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: FixedDelayQueue.h
 * @author: Steven R. Emmerson
 */

#ifndef MISC_FIXEDDELAYQUEUE_H
#define MISC_FIXEDDELAYQUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

/**
 * @tparam Value     Type of value being stored in the queue. Must support
 *                   copy assignment and move assignment.
 * @tparam Dur       Duration type (e.g., std::chrono::seconds)
 */
template<typename Value, typename Dur>
class FixedDelayQueue final
{
public:
    typedef Dur                       Duration;

private:
    typedef std::chrono::steady_clock Clock;
    typedef Clock::time_point         TimePoint;
    /**
     * Implementation of `FixedDelayQueue`.
     */
    class Impl final
    {
        /**
         * An element in the queue.
         */
        class Element final {
            Value     value; /// Value.
            TimePoint when;  /// Reveal-time.

        public:
            /**
             * Constructs from a value and a delay.
             * @param[in] value  Value
             * @param[in] delay  Delay for element until reveal-time
             */
            Element(const Value&    value,
                    const Duration& delay)
                : value{}
                , when{Clock::now() + delay}
            {
                this->value = value;
            }

            /**
             * Returns the value.
             * @return           Value
             * @exceptionsafety  Strong guarantee
             * @threadsafety     Safe
             */
            inline Value getValue() const noexcept
            {
                return value;
            }

            /**
             * Returns the reveal-time.
             * @return           Reveal-time.
             * @exceptionsafety  Strong guarantee
             * @threadsafety     Safe
             */
            inline const TimePoint& getTime() const noexcept
            {
                return when;
            }
        };

        /// The mutex for concurrent access to the queue.
        std::mutex mutable      mutex;
        /// The condition variable for signaling when the queue has been modified
        std::condition_variable cond;
        /// The queue.
        std::queue<Element>     queue;
        /// Minimum residence-time (i.e., delay-time) for an element in the queue
        Duration                delay;

    public:
        /**
         * Constructs from a delay.
         * @param[in] delay           Delay for each element in units of
         *                            template parameter
         * @throws std::bad_alloc     Necessary memory can't be allocated
         * @throws std::system_error  System error
         */
        explicit Impl(const Duration delay)
            : mutex{}
            , cond{}
            , queue{}
            , delay{delay}
        {}

        /**
         * Adds a value to the queue.
         * @param[in] value  Value to be added
         * @exceptionsafety  Strong guarantee
         * @threadsafety     Safe
         */
        void push(const Value& value)
        {
            std::unique_lock<std::mutex>(mutex);
            queue.emplace(value, delay);
            cond.notify_one();
        }

        /**
         * Returns the value whose reveal-time is the earliest and not later
         * than the current time and removes it from the queue. Blocks until
         * such a value is available.
         * @return          Value with earliest reveal-time that's not later
         *                  than current time
         * @exceptionsafety Strong guarantee
         * @threadsafety    Safe
         */
        Value pop()
        {
            std::unique_lock<std::mutex> lock(mutex);
            while (queue.size() == 0)
                cond.wait(lock);
            const auto time = queue.front().getTime();
            while (time > Clock::now())
                cond.wait_until(lock, time);
            const auto value = queue.front().getValue();
            queue.pop(); // Not empty => nothrow
            return value;
        }

        /**
         * Returns the number of values in the queue.
         * @return           Number of values in queue
         * @exceptionsafety  Nothrow
         * @threadsafety     Safe
         */
        size_t size() const noexcept
        {
            std::lock_guard<std::mutex> lock(mutex);
            return queue.size();
        }
    };
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs from a delay.
     * @param[in] delay           Delay for each element in units of template
     *                            parameter
     * @throws std::bad_alloc     Necessary memory can't be allocated
     * @throws std::system_error  System error
     */
    explicit FixedDelayQueue(Dur delay)
        : pImpl{new Impl{delay}}
    {}

    /**
     * Adds a value to the queue.
     * @param[in] value  Value to be added
     * @exceptionsafety  Strong guarantee
     * @threadsafety     Safe
     */
    void push(const Value& value)
    {
        pImpl->push(value);
    }

    /**
     * Returns the value whose reveal-time is the earliest and not later than
     * the current time and removes it from the queue. Blocks until such a value
     * is available.
     * @return           Value with earliest reveal-time that's not later than
     *                   current time
     * @exceptionsafety  Basic guarantee
     * @threadsafety     Safe
     */
    Value pop()
    {
        return pImpl->pop();
    }

    /**
     * Returns the number of values in the queue.
     * @return           Number of values in queue
     * @exceptionsafety  Nothrow
     * @threadsafety     Safe
     */
    size_t size() const noexcept
    {
        return pImpl->size();
    }
};

#endif /* MISC_FIXEDDELAYQUEUE_H */

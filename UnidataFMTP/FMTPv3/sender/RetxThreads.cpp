/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: RetxThreads.cpp
 * @author: Steven R. Emmerson
 *
 * This file implements a thread-safe container for retransmission threads.
 */

#include "RetxThreads.h"

#include <mutex>


/**
 * Add a new thread into the list.
 *
 * @param[in] thread        Thread handler created by the caller
 */
void RetxThreads::add(pthread_t& thread)
{
    std::unique_lock<std::mutex> lock(mutex);
    threads.push_front(thread);
}


/**
 * Remove a thread from the list
 *
 * @param[in] thread        Thread handler created by the caller
 */
void RetxThreads::remove(pthread_t& thread) noexcept
{
    class Equals {
    public:
        Equals(pthread_t& thread) : thread(thread) {}
        bool operator() (const pthread_t& other) const {
            return pthread_equal(thread, other);
        }
    private:
        pthread_t& thread;
    };
    std::unique_lock<std::mutex> lock(mutex);
    threads.remove_if(Equals(thread));
}


/**
 * Cancel all threads and empty the list.
 */
void RetxThreads::shutdown() noexcept
{
    std::unique_lock<std::mutex> lock(mutex);
    for (pthread_t& thread: threads)
        (void)pthread_cancel(thread);
    threads.clear();
}

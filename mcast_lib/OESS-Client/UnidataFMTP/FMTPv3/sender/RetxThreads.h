/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: RetxThreads.h
 * @author: Steven R. Emmerson
 *
 * This file defines a thread-safe container for retransmission threads.
 */

#ifndef RETXTHREADS_H_
#define RETXTHREADS_H_

#include <pthread.h>
#include <forward_list>
#include <mutex>


class RetxThreads {
public:
    /**
     * Adds a thread.
     *
     * **Exception Safety:** Strong guarantee
     *
     * @param[in] thread          The thread to be added.
     * @throws    std::bad_alloc  If necessary space couldn't be allocated.
     *                            The instance is unmodified.
     */
    void add(pthread_t& thread);
    /**
     * Removes a thread.
     *
     * @param[in] thread  The thread to be removed.
     */
    void remove(pthread_t& thread) noexcept;
    /**
     * Shuts down all threads by calling `pthread_cancel()` on each one and
     * empties the container.
     */
    void shutdown() noexcept;

private:
    std::mutex                   mutex;
    std::forward_list<pthread_t> threads;
};

#endif /* RETXTHREADS_H_ */

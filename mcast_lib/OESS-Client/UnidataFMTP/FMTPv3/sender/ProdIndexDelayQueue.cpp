/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ProdIndexDelayQueue.cpp
 * @author: Steven R. Emmerson
 *
 * This file implements a thread-safe delay-queue of product-indexes.
 */


#include "ProdIndexDelayQueue.h"


/**
 * Constructs an instance.
 *
 * @param[in] index    The product-index.
 * @param[in] seconds  The duration, in seconds, from the current time until the
 *                     reveal-time of the element. May be negative.
 */
ProdIndexDelayQueue::Element::Element(
        const uint32_t index,
        const double    seconds)
:
    index(index),
    when(std::chrono::system_clock::now() +
            std::chrono::duration_cast<std::chrono::system_clock::duration>
            (std::chrono::duration<double>(seconds)))
{
}


/**
 * Indicates if the time of the current element is later than another element.
 *
 * @param[in] that  The other element.
 * @retval    true  If and only if the time of this element is later than the
 *                  other element.
 */
bool ProdIndexDelayQueue::Element::isLaterThan(
        const Element& that) const
{
    return this->when > that.when;
}


/**
 * Indicates if the priority of an element is lower than the priority of another
 * element.
 *
 * @param[in] a     The first element.
 * @param[in] b     The second element.
 * @retval    true  If and only if the priority of the first element is less
 *                  than the priority of the second element.
 */
bool ProdIndexDelayQueue::isLowerPriority(
        const Element& a,
        const Element& b)
{
    return a.isLaterThan(b);
}


/**
 * Constructs an instance.
 */
ProdIndexDelayQueue::ProdIndexDelayQueue()
:
    mutex(),
    cond(),
    priQ(&isLowerPriority),
    disabled(false)
{
}


/**
 * Adds an element to the queue.
 *
 * @param[in] index    The product-index.
 * @param[in] seconds  The duration, in seconds, to the reveal-time of the
 *                     product-index (i.e., until the element can be retrieved
 *                     via `pop()`).
 * @throws std::runtime_error  If `disable()` has been called.
 */
void ProdIndexDelayQueue::push(
        const uint32_t index,
        const double   seconds)
{
    std::unique_lock<std::mutex>(mutex);
    throwIfDisabled();
    priQ.push(Element(index, seconds));
    cond.notify_one();
}


/**
 * Returns the time associated with the highest-priority element in the queue.
 *
 * **Exception Safety:** Basic guarantee
 *
 * @pre        The instance is locked.
 * @param[in]  The lock on the instance.
 * @return     The time at which the earliest element will be ready.
 * @throws std::runtime_error  if `disable()` has been called.
 */
const std::chrono::system_clock::time_point&
ProdIndexDelayQueue::getEarliestTime(
        std::unique_lock<std::mutex>& lock)
{
    while (!disabled && priQ.size() == 0)
        cond.wait(lock);
    throwIfDisabled();
    return priQ.top().getTime();
}


/**
 * Returns the product-index whose reveal-time is the earliest and not later
 * than the current time and removes it from the queue. Blocks until such a
 * product-index exists.
 *
 * **Exception Safety:** Basic guarantee
 *
 * @return  The product-index with the earliest reveal-time that's not later
 *          than the current time.
 * @throws std::runtime_error  if `disable()` has been called.
 */
uint32_t ProdIndexDelayQueue::pop()
{
    std::unique_lock<std::mutex> lock(mutex);
    for (std::chrono::system_clock::time_point time = getEarliestTime(lock);
            time > std::chrono::system_clock::now();
            time = getEarliestTime(lock))
        cond.wait_until(lock, time);
    uint32_t index = priQ.top().getIndex();
    priQ.pop();
    cond.notify_one();
    return index;
}


/**
 * Unconditionally returns the product-index whose reveal-time is the earliest
 * and removes it from the queue. Undefined behavior results if the queue is
 * empty.
 *
 * **Exception Safety:** None
 *
 * @return  The product-index with the earliest reveal-time.
 */
uint32_t ProdIndexDelayQueue::get()
{
    std::unique_lock<std::mutex> lock(mutex);
    uint32_t index = priQ.top().getIndex();
    priQ.pop();
    cond.notify_one();
    return index;
}


/**
 * Returns the number of product-indexes in the queue.
 *
 * @return  The number of product-indexes in the queue.
 */
size_t ProdIndexDelayQueue::size() noexcept
{
    std::unique_lock<std::mutex> lock(mutex);
    return priQ.size();
}

void ProdIndexDelayQueue::disable() noexcept
{
    std::unique_lock<std::mutex> lock(mutex);
    disabled = true;
    cond.notify_all();
}

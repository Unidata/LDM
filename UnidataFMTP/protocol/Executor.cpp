/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Executor.cpp
 * @author: Steven R. Emmerson
 *
 * This file defines an executor of independent tasks that allows the caller to
 * obtain the tasks in the order in which they complete.
 */

#include "Executor.h"

#include <stdexcept>
#include <string>
#include <string.h>

using namespace std;

Executor::~Executor() {
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    active.WipSet::~set();
    completed.WipList::~list();
}

Wip* Executor::submit(
        Task&           task)
{
    Wip* wip = new Wip(*this, task);

    try {
        addToActive(wip);

        try {
            pthread_t thread;
            if (int status = pthread_create(&thread, task.getAttributes(),
                    Wip::start, wip))
                throw runtime_error(string("Couldn't create new thread: ") +
                        strerror(status));

            wip->setThread(thread);

            return wip;
        }
        catch (exception& e) {
            removeFromActive(wip);
            throw;
        }
    }
    catch (exception& e) {
        delete wip;
        throw;
    }
}

void* Wip::start(
        void* arg)
{
    Wip* wip = (Wip*)arg;

    try {
        wip->result = wip->task.start();
        wip->executor.moveToCompleted(wip);
        return wip->result;
    }
    catch (std::exception& e) {
        wip->except = &e;
        return 0;
    }
}

void Wip::stop()
{
    stopped = true;
    task.stop();
}

Executor::Lock::Lock(pthread_mutex_t& mutex)
    : mutex(mutex)
{
    if (int status = pthread_mutex_lock(&mutex))
        throw runtime_error(string("Couldn't lock mutex: ") + strerror(status));
}

Executor::Lock::~Lock()
{
    if (int status = pthread_mutex_unlock(&mutex))
        throw runtime_error(string("Couldn't unlock mutex: ") +
                strerror(status));
}

void Executor::addToActive(
        Wip* wip)
{
    Lock                                              lock(mutex);
    pair<set<Wip*,bool(*)(Wip*,Wip*)>::iterator,bool> pair = active.insert(wip);

    if (!pair.second)
        throw runtime_error(string("Task already in active set"));
}

void Executor::removeFromActive(
        Wip* wip)
{
    Lock lock(mutex);
    (void)active.erase(wip);    // don't care if it's not there
}

Wip* Executor::wait()
{
    Lock lock(mutex);
    while (completed.size() == 0)
        (void)pthread_cond_wait(&cond, &mutex);
    Wip* wip = completed.front();
    completed.pop_front();
    (void)pthread_join(wip->getThread(), 0);
    return wip;
}

void Executor::moveToCompleted(
        Wip* wip)
{
    Lock lock(mutex);
    completed.push_back(wip);
    (void)active.erase(wip);    // don't care if it's not there
    (void)pthread_cond_signal(&cond);
}

size_t Executor::numCompleted()
{
    Lock lock(mutex);
    return completed.size();
}

void Executor::stopAllAndClear()
{
    for (std::set<Wip*>::iterator iter = active.begin(); iter != active.end();
            ++iter) {
        (*iter)->stop();        // requires non-blocking
    }

    while (active.size() > 0 || completed.size() > 0) {
        Wip* wip = wait();
        delete wip;
    }
}

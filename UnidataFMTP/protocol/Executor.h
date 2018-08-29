/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Executor.h
 * @author: Steven R. Emmerson
 *
 * This file declares an executor of independent tasks that allows the caller to
 * obtain the tasks in the order in which they complete.
 */

#ifndef EXECUTOR_H_
#define EXECUTOR_H_

#include "Task.h"

#include <pthread.h>
#include <list>
#include <set>

class Wip;

typedef std::set<Wip*>   WipSet;
typedef std::list<Wip*>  WipList;

class Executor {
friend class Wip;

public:
             Executor() :
                active(),
                completed() {}
            ~Executor();
    /**
     * Submits a task for execution on an independent thread.
     *
     * @param[in] task                Task to execute on independent thread.
     * @return                        A work-in-progress.
     * @throws    std::runtime_error  If new thread couldn't be created.
     */
    Wip*     submit(Task& task);
    /**
     * Removes and returns the oldest, completed work-in-progress. Blocks until
     * that WIP is available.
     *
     * @return  Oldest, completed WIP. Caller should delete when it's no longer
     *          needed.
     */
    Wip*     wait();            // blocks
    /**
     * Returns the number of completed works-in-progress.
     *
     * @return  Number of completed works-in-progress.
     */
    size_t   numCompleted();
    /**
     * Stops and deletes all works-in-progress. Upon return, `numCompleted()`
     * will return 0;
     */
    void     stopAllAndClear(); // blocks

private:
    /**
     * Adds a work-in-progress to the set of active works-in-progress.
     *
     * @param[in] wip                 Work-in-progress to add.
     * @throws    std::runtime_error  If a system error occurs.
     */
    void     addToActive(Wip* wip);
    void     removeFromActive(Wip* wip);
    void     moveToCompleted(Wip* wip);

    class Lock {
    public:
         Lock(pthread_mutex_t& mutex);
        ~Lock();
    private:
        pthread_mutex_t& mutex;
    };

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;
    WipSet          active;
    WipList         completed;
};

/**
 * Class `Wip` is declared here because it depends on `Executor` and `Executor`
 * depends on it.
 */
class Wip {
public:
                 Wip(Executor& executor, Task& task) :
                        executor(executor),
                        task(task),
                        thread(0),
                        result(0),
                        stopped(false),
                        except(0) {};
    void         setThread(pthread_t thread)    {this->thread = thread;}
    pthread_t    getThread() const              {return thread;}
    static void* start(void* wip);              // called by `pthread_create()`
    /**
     * Stops a work-in-progress by whatever means necessary. Doesn't block. The
     * user should call this method instead of `Task::stop()` to stop a task
     * that an `Executor` is executing.
     */
    void         stop();
    bool         operator<(Wip* that)           // element comparison in set
            {return this->thread < that->thread;}
    Task&        getTask() const                {return task;}
    bool         wasStopped()                   {return stopped;}
    void         setResult(void* result)        {this->result = result;};
    void*        getResult()                    {return result;};

private:
    Executor&             executor;
    Task&                 task;
    pthread_t             thread;
    void*                 result;
    volatile sig_atomic_t stopped;
    std::exception*       except;
};

#endif /* EXECUTOR_H_ */

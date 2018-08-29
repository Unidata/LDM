/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Task.h
 * @author: Steven R. Emmerson
 *
 * This file declares an abstract base class for a task that will be executed on
 * an independent thread.
 */

#ifndef TASK_H_
#define TASK_H_

#include <pthread.h>
#include <signal.h>

class Task {
public:
                    Task(pthread_attr_t* attr = 0) :
                        attr(attr),
                        result(0) {};
    virtual        ~Task() = 0;         // makes abstract base class
    virtual void*   start() = 0;        // subclasses must implement
    pthread_attr_t* getAttributes()     {return attr;};
    /**
     * Stops this task from executing by whatever means necessary. This method
     * should be called by `Wip::stop()` and not by the user.
     */
    virtual void    stop() {}           // doesn't block

private:
    pthread_attr_t*       attr;
    void*                 result;
};

#endif /* TASK_H_ */

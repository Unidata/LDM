/*
 *   Copyright 2013, University Corporation for Atmospheric Research.
 *   All rights reserved.
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
/*
 * This module comprises a thread-safe wrapper around the LDM product-queue.
 */
#include "config.h"

#include "log.h"
#include "ldmProductQueue.h"    /* Eat own dog food */
#include "ldm.h"
#include "pq.h"
#include "globals.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct LdmProductQueue {
    char*           path;       /**< Pathname of the LDM product-queue */
    pqueue*         pq;         /**< The actual LDM product-queue */
    pthread_mutex_t mutex;      /**< concurrent-access mutex */
};

/**
 * Returns the pathname of the LDM product-queue.
 *
 * @return Pathname of the LDM product-queue.
 */
const char* lpqGetQueuePath(void)
{
    return
        getQueuePath();
}

/**
 * Returns the LDM product-queue that corresponds to a pathname.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S failure. \link log_start() \endlink called.
 * @retval 3    Couldn't open product-queue. \link log_start() \endlink called.
 */
int lpqGet(
    const char*             pathname,   /**< [in] LDM product-queue pathname or
                                          *  NULL to obtain the default queue */
    LdmProductQueue** const lpq)        /**< [out] Pointer to pointer to be set
                                         *  to address of corresponding LDM
                                         *  product-queue. */
{
    int                         status = 0;     /* default success */
    static pthread_mutex_t      mutex = PTHREAD_MUTEX_INITIALIZER;

    if ((status = pthread_mutex_lock(&mutex)) != 0) {
        LOG_ERRNUM0(status, "Couldn't lock mutex");
        status = 2;
    }
    else {
        int                         queueIndex;     /* index of queue */
        static int                  queueCount = 0; /* number of queues */
        static LdmProductQueue**    queues;         /* array of unique queues */

        if (NULL == pathname) {
            pathname = getQueuePath();
        }

        for (queueIndex = 0; queueIndex < queueCount; queueIndex++) {
            if (strcmp(queues[queueIndex]->path, pathname) == 0)
                break;
        }

        if (queueIndex >= queueCount) {
            LdmProductQueue**    newArray = (LdmProductQueue**)realloc(queues,
                    (queueCount+1)*sizeof(LdmProductQueue*));

            if (NULL == newArray) {
                LOG_SERROR1("Unable to allocate new LdmProductQueue array: "
                        "queueCount=%d", queueCount);
                status = 2;
            }
            else {
                LdmProductQueue*    newLpq =
                    (LdmProductQueue*)malloc(sizeof(LdmProductQueue));

                if (NULL == newLpq) {
                    LOG_SERROR0("Unable to allocate new LdmProductQueue");
                    status = 2;
                }
                else {
                    pqueue* pq;
                    int     err = pq_open(pathname, PQ_DEFAULT, &pq);

                    if (err) {
                        LOG_ERRNUM1(err, "Couldn't open product-queue \"%s\"",
                                pathname);
                        status = 3;
                    }
                    else {
                        char* path = strdup(pathname);

                        if (NULL == path) {
                            LOG_SERROR1("Couldn't duplicate string \"%s\"",
                                    pathname);
                            status = 2;
                        }
                        else {
                            pthread_mutex_t mutex;

                            if ((status = pthread_mutex_init(&mutex, NULL)) !=
                                    0) {
                                LOG_ERRNUM0(status,
                                        "Couldn't initialize mutex");
                                free(path);
                                status = 2;
                            }
                            else {
                                newLpq->path = path;
                                newLpq->pq = pq;
                                newLpq->mutex = mutex;
                                newArray[queueCount++] = newLpq;
                                queues = newArray;
                            }
                        } // `path` allocated

                        if (0 != status)
                            (void)pq_close(pq);
                    }                       /* "pq" open */

                    if (0 != status)
                        free(newLpq);
                }                           /* "newLpq" allocated */

                if (0 != status)
                    free(newArray);
            }                               /* "newArray" allocated */
        }                                   /* if new product-queue */

        if (0 == status)
            *lpq = queues[queueIndex];

        (void)pthread_mutex_unlock(&mutex);
    }                                       /* "mutex" locked */

    return status;
}

/**
 * Inserts a data-product into an LDM product-queue.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success. Product inserted into queue.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S error. \link log_start() \endlink called.
 * @retval 3    Product already in queue.
 * @retval 4    Product-queue error. \link log_start() \endlink called.
 */
int lpqInsert(
    LdmProductQueue* const  lpq,    /**< LDM product-queue to insert data-
                                     *   product into. */
    const product* const    prod)   /**< LDM data-product to be inserted */
{

    int status = 0;                 /* default success */

    if ((status = pthread_mutex_lock(&lpq->mutex)) != 0) {
        LOG_ERRNUM0(status, "Couldn't lock mutex");
        status = 2;
    }
    else {
        if ((status = pq_insert(lpq->pq, prod)) != 0) {
            if (PQUEUE_DUP == status) {
                status = 3;
            }
            else {
                LOG_START1("Couldn't insert product into queue: status=%d",
                        status);
                status = 4;
            }
        }

        (void)pthread_mutex_unlock(&lpq->mutex);
    }                                   /* "lpq->mutex" locked */

    return status;
}

/**
 * Closes an LDM product-queue.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success. Product inserted into queue.
 * @retval 1    Precondition failure. \link log_start() \endlink called.
 * @retval 2    O/S error. \link log_start() \endlink called.
 */
int lpqClose(
    LdmProductQueue* const  lpq)    /**< LDM product-queue */
{
    int status;

    if ((status = pthread_mutex_lock(&lpq->mutex)) != 0) {
        LOG_ERRNUM0(status, "Couldn't lock mutex");
        status = 2;
    }
    else {
        if ((status = pq_close(lpq->pq)) != 0) {
            if (EOVERFLOW == status) {
                LOG_START0("LDM product-queue is corrupt");
                status = 1;             /* precondition error */
            }
            else {
                LOG_SERROR0("Couldn't close LDM product-queue");
                status = 2;
            }
        }

        (void)pthread_mutex_unlock(&lpq->mutex);
    }

    return status;
}

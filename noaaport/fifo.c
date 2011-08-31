/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "noaaportLog.h"
#include "fifo.h"                   /* Eat own dog food */

struct fifo {
    unsigned char*      buf;        /**< Pointer to start of buffer */
    size_t              nextWrite;  /**< Offset to next byte to write */
    size_t              nbytes;     /**< Number of bytes in the buffer */
    size_t              nreserved;  /**< Number of bytes reserved for writing */
    size_t              size;       /**< Size of buffer in bytes */
    pthread_mutex_t     mutex;      /**< Concurrent access lock */
    pthread_mutex_t     writeMutex; /**< Concurrent write access lock */
    pthread_mutex_t     readMutex;  /**< Concurrent read access lock */
    pthread_cond_t      readCond;   /**< Reading condition variable */
    pthread_cond_t      writeCond;  /**< Writing condition variable */
    int                 closeIfEmpty;   /**< Close the FIFO if empty? */
};

/**
 * Initializes a FIFO
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
static int initializeFifo(
    Fifo* const             fifo,   /**< [in/out] Pointer to the FIFO */
    unsigned char* const    buf,    /**< [in] The buffer */
    const size_t            size)   /**< [in] Size of the buffer in bytes */
{
    int                 status = 2; /* default failure */
    pthread_mutexattr_t mutexAttr;

    if ((status = pthread_mutexattr_init(&mutexAttr)) != 0) {
        NPL_ERRNUM0(status, "Couldn't initialize mutex attributes");
        status = 2;
    }
    else {
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);

        if ((status = pthread_mutex_init(&fifo->writeMutex, &mutexAttr)) != 0) {
            NPL_ERRNUM0(status, "Couldn't initialize write-mutex");
            status = 2;
        }
        else {
            if ((status = pthread_mutex_init(&fifo->readMutex, &mutexAttr)) !=
                    0) {
                NPL_ERRNUM0(status, "Couldn't initialize read-mutex");
                status = 2;
            }
            else {
                if ((status = pthread_mutex_init(&fifo->mutex, NULL)) != 0) {
                    NPL_ERRNUM0(status, "Couldn't initialize FIFO mutex");
                    status = 2;
                }
                else {
                    if ((status = pthread_cond_init(&fifo->readCond, NULL)) !=
                            0) {
                        NPL_ERRNUM0(status,
                            "Couldn't initialize reading condition variable");
                        status = 2;
                    }
                    else {
                        if ((status = pthread_cond_init(&fifo->writeCond,
                                        NULL)) != 0) {
                            NPL_ERRNUM0(status,
                            "Couldn't initialize writing condition variable");
                            status = 2;
                        }
                        else {
                            fifo->buf = buf;
                            fifo->nextWrite = 0;
                            fifo->nbytes = 0;  /* indicates startup */
                            fifo->size = size;
                            fifo->nreserved = 0;
                            fifo->closeIfEmpty = 0;
                            status = 0; /* success */
                        }               /* "fifo->writeCond" initialized */

                        if (0 != status)
                            (void)pthread_cond_destroy(&fifo->readCond);
                    }                   /* "fifo->readCond" initialized */

                    if (0 != status)
                        (void)pthread_mutex_destroy(&fifo->mutex);
                }                       /* "fifo->mutex" initialized */
            }

            if (0 != status)
                (void)pthread_mutex_destroy(&fifo->writeMutex);
        }                               /* "fifo->writeMutex" initialized */

        (void)pthread_mutexattr_destroy(&mutexAttr);
    }                                   /* "mutexAttr" initialized */

    return status;
}

/**
 * Returns a FIFO.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
int fifoNew(
    const size_t        npages,         /**< [in] FIFO size in pages */
    Fifo** const        fifo)           /**< [out] Pointer to pointer to be set
                                         *   to address of FIFO */
{
    int                 status = 2;     /* default failure */
    Fifo*               f = (Fifo*)malloc(sizeof(Fifo));

    if (NULL == f) {
        NPL_SERROR0("Couldn't allocate FIFO");
    }
    else {
        const long              pagesize = sysconf(_SC_PAGESIZE);
        const size_t            size = npages*pagesize;
        unsigned char* const    buf = (unsigned char*)malloc(size);

        if (NULL == buf) {
            NPL_SERROR1("Couldn't allocate %lu bytes for FIFO buffer",
                    (unsigned long)size);
        }
        else {
            if ((status = initializeFifo(f, buf, size)) == 0)
                *fifo = f;

            if (0 != status)
                free(buf);
        }                               /* "buf" allocated */

        if (0 != status)
            free(f);
    }                                   /* "f" allocated */

    return status;
}

/**
 * Reserves space in a FIFO and returns a pointer to it. Blocks until the
 * requested amount of space is available. The client should subsequently call
 * \link fifoUpdate() \endlink when the space has been written into or \link
 * fifoCopy() \endlink. Only one thread can execute this function and the
 * subsequent \link fifoUpdate() \endlink or \link fifoCopy() \endlink at a
 * time.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success. \c *bytes points to at least \c nbytes of memory.
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S error. \c nplStart() called.
 * @retval 3    FIFO is closed.
 */
int fifoWriteReserve(
    Fifo* const             fifo,       /**< [in/out] Pointer to the FIFO */
    const size_t            nbytes,     /**< [in] The amount of space to
                                         *   reserve */
    unsigned char** const   buf,        /**< [out] Pointer to the pointer to be
                                         *   set to the address of the reserved
                                         *   space */
    size_t* const           size)       /**< [out] Amount of data, in bytes, 
                                         *   that can be initially transferred.
                                         *   If less than \e nbytes, then the
                                         *   user must subsequently call
                                         *   \c fifoCopy(); otherwise, the user
                                         *   may call \c fifoCopy() or \c
                                         *   write into \e buf and then call
                                         *   \c fifoWriteUpdate(). */
{
    int             status;

    if (nbytes > fifo->size) {
        NPL_START2("Requested space larger than FIFO: %lu > %lu", nbytes,
                fifo->size);
        status = 1;
    }
    else {
        if ((status = pthread_mutex_lock(&fifo->writeMutex)) != 0) {
            NPL_ERRNUM0(status, "Couldn't lock write-mutex");
            status = 2;
        }
        else {
            if ((status = pthread_mutex_lock(&fifo->mutex)) != 0) {
                NPL_ERRNUM0(status, "Couldn't lock FIFO mutex");
                status = 2;
            }
            else {
                for (;;) {
                    if (fifo->closeIfEmpty && 0 == fifo->nbytes) {
                        status = 3;
                        break;
                    }

                    if ((fifo->size - fifo->nbytes) >= nbytes) {
                        *buf = fifo->buf + fifo->nextWrite;
                        *size = fifo->size - fifo->nextWrite;
                        fifo->nreserved = nbytes;
                        break;
                    }

                    if ((status = pthread_cond_wait(&fifo->writeCond,
                                    &fifo->mutex)) != 0) {
                        NPL_ERRNUM0(status,
                                "Couldn't wait on writing condition variable");
                        status = 2;
                        break;
                    }
                }                       /* condition wait-loop */

                (void)pthread_mutex_unlock(&fifo->mutex);
            }                           /* "fifo->mutex" locked */

            if (0 != status)
                (void)pthread_mutex_unlock(&fifo->writeMutex);
        }                               /* "fifo->writeMutex" locked */
    }                                   /* "nbytes" OK */

    return status;
}

/**
 * Updates the FIFO based on a successful write to the space obtained from
 * \link fifoWriteReserve() \endlink. This function must be called by the
 * thread that returned from the previous call to \link fifoWriteReserve()
 * \endlink. 
 *
 * This function in thread-safe.
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S error. \c nplStart() called.
 */
int fifoWriteUpdate(
    Fifo* const     fifo,   /**< [in/out] Pointer to FIFO */
    const size_t    nbytes) /**< [in] The number of bytes actually written */
{
    int             status;

    if ((status = pthread_mutex_lock(&fifo->mutex)) != 0) {
        NPL_ERRNUM0(status, "Couldn't lock FIFO mutex");
        status = 2;
    }
    else {
        if (nbytes > fifo->nreserved) {
            NPL_START2("Amount written > amount reserved: %lu > %lu",
                    (unsigned long)nbytes, 
                    (unsigned long)fifo->nreserved);
            status = 1;                 /* usage error */
        }
        else {
            if ((status = pthread_cond_signal(&fifo->readCond)) != 0) {
                NPL_ERRNUM0(status,
                        "Couldn't signal reading condition variable");
                status = 2;             /* Usage error */
            }
            else {
                fifo->nextWrite = (fifo->nextWrite + nbytes) % fifo->size;
                fifo->nbytes += nbytes;
                fifo->nreserved = 0;

                if ((status = pthread_mutex_unlock(&fifo->writeMutex)) != 0) {
                    NPL_ERRNUM0(status, "Couldn't unlock write-mutex");
                    status = 1;         /* Usage error */
                }
                else {
                    status = 0;         /* success */
                }
            }
        }

        (void)pthread_mutex_unlock(&fifo->mutex);
    }                                   /* "fifo->mutex" locked */

    return status;
}

/**
 * Copies bytes into the fifo.  This function must be called by the thread that
 * returned from the previous call to \link fifoWriteReserve() \endlink. 
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S error. \c nplStart() called.
 */
int fifoCopy(
    Fifo* const             fifo,   /**< [in] The FIFO */
    unsigned char* const    buf,    /**< [in] The buffer to copy from */
    const size_t            nbytes) /**< [in] The number of bytes to copy */
{
    int             status;

    if ((status = pthread_mutex_lock(&fifo->mutex)) != 0) {
        NPL_ERRNUM0(status, "Couldn't lock FIFO mutex");
        status = 2;
    }
    else {
        if (nbytes > fifo->nreserved) {
            NPL_START2("Amount to copy > amount reserved: %lu > %lu",
                    (unsigned long)nbytes, 
                    (unsigned long)fifo->nreserved); 
            status = 1;                 /* usage error */
        }
        else {
            const size_t    avail = fifo->size - fifo->nextWrite;
            size_t          n = avail < nbytes ? avail : nbytes;

            (void)memcpy(fifo->buf + fifo->nextWrite, buf, n);

            if (n < nbytes)
                (void)memcpy(fifo->buf, buf + n, nbytes - n);

            if ((status = pthread_cond_signal(&fifo->readCond)) != 0) {
                NPL_ERRNUM0(status,
                        "Couldn't signal reading condition variable");
                status = 2;             /* Usage error */
            }
            else {
                fifo->nextWrite = (fifo->nextWrite + nbytes) % fifo->size;
                fifo->nbytes += nbytes;
                fifo->nreserved = 0;

                if ((status = pthread_mutex_unlock(&fifo->writeMutex)) != 0) {
                    NPL_ERRNUM0(status, "Couldn't unlock write-mutex");
                    status = 1;         /* Usage error */
                }
                else {
                    status = 0;         /* success */
                }
            }
        }

        (void)pthread_mutex_unlock(&fifo->mutex);
    }                                   /* "fifo->mutex" locked */

    return status;
}

/**
 * Returns a pointer to the next FIFO region to be read from. Blocks until
 * sufficient data exists. Only one thread can execute this function and the
 * subsequent \link fifoReadUpdate() \endlink at a time.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S error. \c nplStart() called.
 * @retval 3    FIFO is closed.
 */
int fifoRead(
    Fifo* const             fifo,   /**< [in/out] Pointer to FIFO */
    unsigned char* const    buf,    /**< [out] Buffer into which to put data */
    const size_t            nbytes) /**< [in] The number of bytes to be read */
{
    int status;

    if (nbytes > fifo->size) {
        NPL_START2("Requested read amount is larger than FIFO: %lu > %lu",
                nbytes, fifo->size);
        status = 1;
    }
    else {
        if ((status = pthread_mutex_lock(&fifo->readMutex)) != 0) {
            NPL_ERRNUM0(status, "Couldn't lock read-mutex");
            status = 2;
        }
        else {
            if ((status = pthread_mutex_lock(&fifo->mutex)) != 0) {
                NPL_ERRNUM0(status, "Couldn't lock FIFO mutex");
                status = 2;
            }
            else {
                for (;;) {
                    if (fifo->closeIfEmpty && 0 == fifo->nbytes) {
                        status = 3;
                        break;
                    }

                    if (fifo->nbytes >= nbytes)
                        break;

                    if ((status = pthread_cond_wait(&fifo->readCond,
                                    &fifo->mutex)) != 0) {
                        NPL_ERRNUM0(status,
                                "Couldn't wait on reading condition variable");
                        status = 2;
                        break;
                    }
                }

                if (0 == status) {
                    if ((status = pthread_cond_signal(&fifo->writeCond)) != 0) {
                        NPL_ERRNUM0(status,
                                "Couldn't signal writing condition variable");
                        status = 2;
                    }
                    else {
                        ssize_t nextRead = fifo->nextWrite - fifo->nbytes;
                        size_t  avail;
                        size_t  n;

                        if (0 > nextRead)
                            nextRead += fifo->size;

                        avail = fifo->size - nextRead;
                        n = avail < nbytes ? avail : nbytes;

                        (void)memcpy(buf, fifo->buf + nextRead, n);

                        if (n < nbytes)
                            (void)memcpy(buf + n, fifo->buf, nbytes - n);

                        fifo->nbytes -= nbytes;
                    }
                }

                (void)pthread_mutex_unlock(&fifo->mutex);
            }                           /* "fifo->mutex" locked */

            (void)pthread_mutex_unlock(&fifo->readMutex);
        }                               /* "fifo->readMutex" locked */
    }                                   /* "nbytes" OK */

    return status;
}

/**
 * Closes a FIFO when it becomes empty. Attempting to write to or read from a
 * closed FIFO will result in an error. Blocked \link fifoWriteReserve() 
 * \endlink and \link fifoRead() \endlink operations will error-return.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c nplStart() called.
 * @retval 2    O/S error. \c nplStart() called.
 */
int fifoCloseWhenEmpty(
    Fifo* const fifo)       /**< [in/out] Pointer to FIFO */
{
    int         status = 0; /* default success */

    if ((status = pthread_mutex_lock(&fifo->mutex)) != 0) {
        NPL_ERRNUM0(status, "Couldn't lock mutex");
        status = 2;
    }
    else {
        fifo->closeIfEmpty = 1;

        if ((status = pthread_cond_signal(&fifo->writeCond)) != 0) {
            NPL_ERRNUM0(status, "Couldn't signal writing condition variable");
            status = 2;
        }
        if ((status = pthread_cond_signal(&fifo->readCond)) != 0) {
            NPL_ERRNUM0(status, "Couldn't signal reading condition variable");
            status = 2;
        }

        (void)pthread_mutex_unlock(&fifo->mutex);
    }                                   /* "fifo->mutex" locked */

    return status;
}

/**
 * Copyright 2013, University Corporation for Atmospheric Research
 * <p>
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 *
 * @author Steven R. Emmerson
 */

/*LINTLIBRARY*/

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

#include "ldm.h"
#include "error.h"
#include "timestamp.h"
#include "ulog.h"
#include "globals.h"
#include "remote.h"


/**
 * Returns the current time.
 *
 * @return      Pointer to static buffer containing the current time
 */
static const timestampt*
getTime(void)
{
    static timestampt   now;
    int                 error = set_timestamp(&now);

    assert(error == 0);

    return &now;
}


/******************************************************************************
 * Begin Queue Module
 *
 * Client-defined data-objects are added to the tail of the queue and removed
 * from the head of the queue.
 ******************************************************************************/


typedef struct elt {
    void*               data;           /* the client-defined data-object */
    struct elt*         next;           /* pointer to next element */
} Elt;

static Elt*             q_head = NULL;  /* pointer to head element */
static Elt*             q_tail = NULL;  /* pointer to tail element */


/**
 * Returns the head of the queue.
 *
 * @return          Pointer to a pointer to the client-defined data-object at
 *                  the head of the queue or NULL if the queue is empty
 */
#define q_getHead() ((void* const*)q_head)


/**
 * Adds a client-defined data-object to the tail of the queue.
 *
 * @param data          The data-object to be added. Shall not be NULL. The
 *                      client shall assume responsibility for allocating and
 *                      deallocating the data-object as necessary.
 * @retval 0            Success
 * @retval EINVAL       "data" is NULL
 * @retval ENOMEM       Out-of-memory
 */
static int
q_add(
    void* const data)
{
    int         status;

    if (data == NULL) {
        status = EINVAL;
    }
    else {
        Elt*    elt = malloc(sizeof(Elt));

        if (NULL == elt) {
            status = ENOMEM;
        }
        else {
            elt->data = data;
            elt->next = NULL;

            if (q_head == NULL)
                q_head = elt;

            if (q_tail != NULL)
                q_tail->next = elt;

            q_tail = elt;
            status = 0;
        }
    }

    return status;
}


/**
 * Removes and returns the client-defined data-object at the head of the queue.
 *
 * @return      Pointer to the client-defined data-object that was at the head
 *              of the queue or NULL if the queue was empty
 */
static void*
q_removeHead(void)
{
    void*    data;

    if (q_head == NULL) {
        data = NULL;
    }
    else {
        Elt*    newHead = q_head->next;

        data = q_head->data;

        free(q_head);

        if (newHead == NULL)
            q_tail = NULL;

        q_head = newHead;
    }

    return data;
}


/**
 * Returns the client-defined data-object that is immediately closer to the
 * tail of the queue than a given element in the queue.
 *
 * @param ptr       Pointer returned by "q_getHead()" or "q_getNext()". Shall
 *                  not be NULL.
 * @return          Pointer to a pointer to the client-defined data-object that
 *                  is next in the queue or NULL if no such object exists
 */
#define q_getNext(ptr)  ((void* const*)((Elt*)ptr)->next)


/******************************************************************************
 * Begin Statistics Module
 ******************************************************************************/


/**
 * An entry in the queue
 */
typedef struct {
    timestampt          time;           /* when the entry was created */
    int                 wasAccepted;    /* if the data-product was inserted */
} Entry;


static timestampt   s_prevCompTime;    /* time of previous computation */
static unsigned     s_ldmCount = 0;    /* number of LDM-s receiving same data */
static int          s_primary;         /* LDM uses HEREIS exclusively? */
static int          s_switch = 0;      /* LDM process should switch mode? */


/**
 * Indicates whether or not this LDM process should switch its data-product
 * receive-mode.
 *
 * @retval 0    The LDM shouldn't switch
 * @retval 1    The LDM should switch
 */
#define s_getSwitch()         s_switch


/**
 * Sets the mode of this LDM process.
 *
 * @param primary       Whether or not this LDM process is in primary data
 *                      receive-mode
 */
#define s_setPrimary(primary)          (s_primary = primary)


/**
 * Indicates whether or not this LDM process is in primary receive-mode.
 *
 * @retval 0    The LDM is not in primary mode
 * @retval 1    The LDM is in primary mode
 */
#define s_isPrimary()          s_primary


/**
 * Returns the number of LDM processes that are receiving the same data. The
 * number is initially 0.
 *
 * @return      The number of LDM processes receiving the same data
 */
#define s_getLdmCount()          s_ldmCount


/**
 * Sets the number of LDM processes that are receiving the same data.
 *
 * @return      The number of LDM processes receiving the same data
 */
#define s_setLdmCount(count)     (s_ldmCount = count)


/**
 * Resets the state of summary statistics. NB: The number of LDM-s receiving
 * the same data is not modified.
 */
static void
s_reset(void)
{
    s_prevCompTime = *getTime();
    s_switch = 0;

    while (q_getHead() != NULL)
        free(q_removeHead());
}


/**
 * Processes the acceptance or rejection of a data-product. Only meaningful
 * if the number of LDM processes receiving the same data is greater than 1.
 * <p>
 * After this function is called, "s_getSwitch()" might return a different
 * value than before.
 *
 * @param accepted      [in] Whether or not this data-product was successfully
 *                      inserted into the product-queue
 * @retval 0            Success
 * @retval ENOSYS       as_getLdmCount() <= 1
 * @retval ENOMEM       Out-of-memory
 */
static int
s_process(
    const int       accepted)
{
    int             status;

    /*
     * Is calling this function meaningful?
     */
    if (s_ldmCount <= 1) {
        /* No */
        status = ENOSYS;
    }
    else {
        /* Yes */
        Entry* const    newestEntry = malloc(sizeof(Entry));

        if (newestEntry == NULL) {
            status = ENOMEM;
        }
        else {
            timestampt      now = *getTime();

            newestEntry->time = now;
            newestEntry->wasAccepted = accepted;

            if ((status = q_add(newestEntry)) == 0) {
                const void* const*  elt;
                const double        period = d_diff_timestamp(&now, &s_prevCompTime);

                /*
                 * Reduce memory usage by purging the queue of entries that are
                 * too old.
                 */
                for (elt = q_getHead(); elt != NULL; elt = q_getHead()) {
                    if (tvCmp((*(const Entry* const*)elt)->time,
                            s_prevCompTime, >))
                        break;

                    free(q_removeHead());
                }

                /*
                 * Has sufficient time elapsed for a performance comparison?
                 */
                if (period < 2*interval) {  /* SWAG threshold */
                    /* No */
                    s_switch = 0;
                    udebug("s_process(): period=%g s", period);
                }
                else {
                    /* Yes */
                    unsigned acceptedCount = 0;
                    unsigned rejectedCount = 0;

                    /*
                     * Obtain the data
                     */
                    for (elt = q_getHead(); elt != NULL; elt = q_getNext(elt))
                        (*(const Entry* const*)elt)->wasAccepted
                                ? ++acceptedCount
                                : ++rejectedCount;

                    /*
                     * Is there sufficient data for a performance comparison?
                     */
                    if (acceptedCount + rejectedCount == 0) {
                        /* No */
                        s_switch = 0;

                        udebug("s_process(): period=%g s, #accept=%u, "
                                "#reject=%u",
                            period, acceptedCount, rejectedCount);
                    }
                    else {
                        /* Yes */
                        const double      rejectedMean = rejectedCount /
                                (double)(s_ldmCount - 1);

                        s_switch = s_primary
                                    ? (acceptedCount <= rejectedMean)
                                    : (acceptedCount >= rejectedMean);

                        udebug("s_process(): period=%g s, #accept=%u, "
                                "#reject=%u, #LDM-s=%u, primary=%d, switch=%d",
                            period, acceptedCount, rejectedCount, s_ldmCount,
                            s_primary, s_switch);
                    }

                    s_prevCompTime = now;
                }                       /* sufficient time has elapsed */

                status = 0;
            }                           /* newest entry added */

            if (status)
                free(newestEntry);
        }                               /* "newestEntry" allocated */
    }                                   /* "s_ldmCount" > 1 */

    return status;
}


/******************************************************************************
 * Begin public interface
 ******************************************************************************/


/**
 * Sets the number of LDM-s receiving the same data.  If the number doesn't
 * equal the previous number, then "as_init()" is called.
 *
 * @param count     The number of LDM-s receiving the same data.
 * @retval 0        Success
 * @retval EINVAL  "count" is zero.  The previous number will be unchanged.
 */
int
as_setLdmCount(
    unsigned    count)
{
    if (count == 0)
        return EINVAL;

    if (count != s_getLdmCount()) {
        s_reset();
        s_setLdmCount(count);
    }

    return 0;
}


/**
 * Resets this module. Starts the clock on measuring performance.
 * <p>
 * NB: The number of LDM processes receiving the same data is not modified.
 *
 * @param isPrimary     Whether or not the transmission-mode is primary (i.e.,
 *                      uses HEREIS rather than COMINGSOON/BLKDATA messages)
 * @retval NULL         Success
 */
void
as_init(
    const int   isPrimary)
{
    s_reset();
    s_setPrimary(isPrimary);
}


/**
 * Processes the status of a received data-product.
 *
 * @param success       Whether or not the data-product was inserted into the
 *                      product-queue
 * @param size          Size of the data-product in bytes
 * @retval 0            Success
 * @retval ENOSYS       "as_setLdmCount()" not yet called
 * @retval ENOMEM       Out of memory
 */
int
as_process(
    const int           success,
    const size_t        size)
{
    if (s_getLdmCount() == 0)
        return ENOSYS;

    if (s_getLdmCount() == 1)
        return 0;

    return s_process(success);
}


/**
 * Indicates whether or not this LDM process should switch its data-product
 * receive-mode. Always returns 0 if "as_setLdmCount()" has not been called.
 *
 * @retval 0       Don't switch
 * @retval 1       Do switch
 */
int
as_shouldSwitch(void)
{
    return
        (s_getLdmCount() == 0)             /* not a downstream LDM */
            ? 0
            : (s_getLdmCount() == 1)
                ? !s_isPrimary()
                : s_getSwitch();
}

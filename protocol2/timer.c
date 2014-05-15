/*
 * A countdown timer.
 *
 * See ../COPYRIGHT file for copying and redistribution conditions.
 */
#include <config.h>

#include <stddef.h>

#include <log.h>
#include <timestamp.h>

#include <timer.h>

struct timer {
    timestampt          started;
    unsigned long       seconds;
};

/*
 * Starts a countdown timer.
 *
 * Arguments:
 *      seconds         The coundown time-interval in seconds.
 * Returns:
 *      NULL            Error. "log_start()" called.
 *      else            Pointer to the timer data-structure.
 */
Timer*
timer_new(
    const unsigned long seconds)
{
    const size_t        nbytes = sizeof(Timer);
    Timer*              timer = (Timer*)malloc(nbytes);

    if (NULL == timer) {
        LOG_SERROR1("Couldn't allocate %lu bytes", nbytes);
    }
    else {
        if (0 != set_timestamp(&timer->started)) {
            LOG_SERROR0("Couldn't get time");
            free(timer);
            timer = NULL;
        }
        else {
            timer->seconds = seconds;
        }
    }

    return timer;
}

/*
 * Frees a timer.
 *
 * Arguments
 *      timer           Pointer to the timer data-structure to be freed or
 *                      NULL.
 */
void
timer_free(
    Timer* const        timer)
{
    free(timer);
}

/*
 * Indicates if a timer has elapsed.
 *
 * Arguments:
 *      timer           The timer data-structure.
 * Returns:
 *      0               The timer has not elapsed.
 *      else            The timer has elapsed.
 */
int
timer_hasElapsed(
    const Timer* const  timer)
{
    timestampt  now;

    (void)set_timestamp(&now);

    return d_diff_timestamp(&now, &timer->started) > timer->seconds;
}

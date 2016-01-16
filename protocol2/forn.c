/**
 * This module contains server-side functions that are common to both version
 * 5 and version 6 of the FEEDME and NOTIFYME messages.
 */

#include "config.h"

#include "forn.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "prod_class.h"

/**
 * Determines if a subscription has been reduced and logs a message if it has.
 *
 * @param origSub   [in] Original subscription
 * @param currSub   [in] Current subscription
 * @param entity    [in] Entity that reduced the subscription. Replaces the
 *                  blank in the sentence "Subscription reduced by one or more
 *                  _____".
 * @retval 0        If and only if the original subscription equals the
 *                  current subscription (i.e., the subscription has not been
 *                  reduced)
 */
int logIfReduced(
        const prod_class_t* const   origSub,
        const prod_class_t* const   currSub,
        const char*                 entity)
{
    int wasReduced = !clss_eq(origSub, currSub);

    if (wasReduced) {
        /*
         * The downstream LDM is not allowed to receive what it requested.
         */
        char origStr[1984];

        (void) s_prod_class(origStr, sizeof(origStr), origSub);
        log_warning("Subscription reduced by one or more %s: %s -> %s", entity,
                origStr, s_prod_class(NULL, 0, currSub));
    }

    return wasReduced;
}

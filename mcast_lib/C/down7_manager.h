/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7_manager.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the manager of downstream LDM-7s.
 */

#ifndef DOWN7_MANAGER_H_
#define DOWN7_MANAGER_H_

#include "inetutil.h"
#include "ldm.h"


#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Adds a potential downstream LDM-7.
 *
 * @param[in] ft           Feedtype to subscribe to.
 * @param[in] ldm          Upstream LDM-7 to which to subscribe.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_start()` called.
 */
Ldm7Status
d7mgr_add(
        const feedtypet    ft,
        ServiceAddr* const ldm);

/**
 * Frees the downstream LDM-7 manager.
 */
void
d7mgr_free(void);

/**
 * Starts all multicast-receiving LDM-7s as individual child processes of the
 * current process and frees the downstream LDM-7 manager.
 *
 * @retval 0            Success.
 * @retval LDM7_SYSTEM  System error. `log_start()` called. Downstream LDM-7
 *                      manager is in an indeterminate state.
 */
int
d7mgr_startAll(void);

#ifdef __cplusplus
    }
#endif

#endif /* DOWN7_MANAGER_H_ */

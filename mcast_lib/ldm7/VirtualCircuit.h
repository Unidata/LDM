/**
 * This file declares things relevant to a virtual circuit.
 *
 *        File: VirtualCircuit.h
 *  Created on: Mar 6, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_LDM7_VIRTUALCIRCUIT_H_
#define MCAST_LIB_LDM7_VIRTUALCIRCUIT_H_

#include "ldm.h"

#include <stdbool.h>

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Initializes a virtual-circuit endpoint.
 * @param[out] vcEnd     Virtual-circuit endpoint to be initialized
 * @param[in]  vlanId    VLAN ID (tag)
 * @param[in]  switchId  Switch identifier (e.g.,
 *                       “sdn-sw.ashb.net.internet2.edu”). Caller may free.
 * @param[in]  portId    Identifier of port on switch (e.g., "1/7”). Caller may
 *                       free.
 * @retval     `true`    Instance initialized
 * @retval     `false`   Instance not initialized. `log_add()` called.
 * @see vcEndPoint_destroy()
 */
bool vcEndPoint_construct(
        VcEndPoint* const restrict vcEnd,
        const VlanId               vlanId,
        const char* const restrict switchId,
        const char* const restrict portId);

/**
 * Returns a new virtual-circuit endpoint.
 * @param[in] vlanId    VLAN ID (tag)
 * @param[in] switchId  Switch identifier (e.g.,
 *                      “sdn-sw.ashb.net.internet2.edu”)
 * @param[in] portId    Identifier of port on switch (e.g., "1/7”)
 * @retval    NULL      Couldn't construct new instance. `log_add()` called.
 * @return              Virtual-circuit endpoint. Caller should call
 *                      `vcEndPoint_delete()` when it's no longer needed.
 * @see vcEndPoint_delete()
 */
VcEndPoint* vcEndPoint_new(
        VlanId      vlanId,
        const char* switchId,
        const char* portId);

/**
 * Returns a string representation of a virtual-circuit endpoint.
 * @param[in] vcEnd   Virtual-circuit endpoint
 * @retval    NULL    Failure. `log_add()` called.
 * @return            String representation. Caller should free when it's no
 *                    longer needed.
 * @threadsafety      Safe
 */
char* vcEndPoint_format(const VcEndPoint* vcEnd);

/**
 * Destroys a virtual-circuit endpoint.
 * @param[in] end  Virtual-circuit endpoint to be destroyed.
 */
void vcEndPoint_destroy(VcEndPoint* const end);

/**
 * Deletes a virtual-circuit endpoint.
 * @param[in] end  Virtual-circuit endpoint to be deleted
 * @see vcEndPoint_new()
 */
void vcEndPoint_delete(VcEndPoint* const end);

bool vcEndPoint_copy(
        VcEndPoint* const restrict       lhs,
        const VcEndPoint* const restrict rhs);

/**
 * Clones a virtual-circuit endpoint.
 * @param[in] end   Endpoint to be cloned
 * @retval    NULL  Couldn't clone instance. `log_add()` called.
 * @return          Clone of `end`. Caller should call `vcEndPoint_delete()` on
 *                  it when it's no longer needed.
 * @see vcEndPoint_delete()
 */
VcEndPoint* vcEndPoint_clone(const VcEndPoint* end);

char* vc_create(
        const char*       name,
        const VcEndPoint* end1,
        const VcEndPoint* end2);

void vc_destroy(const char* vcId);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_VIRTUALCIRCUIT_H_ */

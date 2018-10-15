/**
 * This file defines things relevant to a virtual circuit.
 *
 *        File: VirtualCircuit.h
 *  Created on: Mar 6, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "ldmprint.h"
#include "log.h"
#include "VirtualCircuit.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool
vcEndPoint_init(
        VcEndPoint* const restrict vcEnd,
        const VlanId               vlanId,
        const char*                switchId,
        const char*                portId)
{
    bool success = false;

    // `xdr_string()` requires a non-NULL pointer
    if (switchId == NULL)
        switchId = "dummy";
    if (portId == NULL)
        portId = "dummy";

    char* const dupSwitchId = strdup(switchId);

    if (dupSwitchId == NULL) {
        log_add("Couldn't allocate space for switch identifier");
    }
    else {
        char* const dupPortId = strdup(portId);

        if (dupPortId == NULL) {
            log_add("Couldn't allocate space for port identifier");
        }
        else {
            vcEnd->switchId = dupSwitchId;
            vcEnd->portId = dupPortId;
            vcEnd->vlanId = vlanId;
            success = true;
        } // `dupPortId` allocated

        if (!success)
            free(dupSwitchId);
    } // `dupSwitchId` allocated

    return success;
}

VcEndPoint*
vcEndPoint_new(
        VlanId      vlanId,
        const char* switchId,
        const char* portId)
{
    VcEndPoint* end = log_malloc(sizeof(VcEndPoint),
            "virtual-circuit endpoint");
    if (end) {
        if (!vcEndPoint_init(end, vlanId, switchId, portId)) {
            free(end);
            end = NULL;
        }
    } // `end` allocated
    return end;
}

bool
vcEndPoint_isValid(const VcEndPoint* const vcEnd)
{
    return vcEnd &&
            vcEnd->switchId && strcmp(vcEnd->switchId, "dummy") &&
            vcEnd->portId   && strcmp(vcEnd->portId,   "dummy");
}

char*
vcEndPoint_format(const VcEndPoint* vcEnd)
{
    char* str = ldm_format(256, "{switch=%s, port=%s, vlanId=%hu}",
            vcEnd->switchId, vcEnd->portId, vcEnd->vlanId);
    if (str == NULL)
        log_add("Couldn't format virtual-circuit endpoint");
    return str;
}

void
vcEndPoint_destroy(VcEndPoint* const end)
{
    if (end) {
        free(end->portId);
        end->portId = NULL;

        free(end->switchId);
        end->switchId = NULL;
    }
}

void
vcEndPoint_free(VcEndPoint* const end)
{
    if (end) {
        vcEndPoint_destroy(end);
        free(end);
    }
}

bool
vcEndPoint_copy(
        VcEndPoint* const restrict       lhs,
        const VcEndPoint* const restrict rhs)
{
    return vcEndPoint_init(lhs, rhs->vlanId, rhs->switchId, rhs->portId);
}

VcEndPoint*
vcEndPoint_clone(const VcEndPoint* const end)
{
    return vcEndPoint_new(end->vlanId, end->switchId, end->portId);
}

char* vc_create(
        const char*       name,
        const VcEndPoint* end1,
        const VcEndPoint* end2);

void vc_destroy(const char* vcId);

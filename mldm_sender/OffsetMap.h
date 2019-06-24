/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: OffsetMap.h
 * @author: Steven R. Emmerson
 *
 * This file defines a thread-safe mapping from FMTP product-indexes to
 * file-offsets.
 */

#ifndef MLDM_SENDER_OFFSETMAP_H_
#define MLDM_SENDER_OFFSETMAP_H_

#include "ldm.h"

#include <sys/types.h>

#ifdef __cplusplus

#include <ctime>
#include <unordered_map>
#include <mutex>

class OffsetMap {
public:
    OffsetMap() : map(), mutex() {};
    /**
     * Adds an entry from a product-index to an offset.
     * @param[in] prodIndex  The product-index.
     * @param[in] offset     The offset.
     */
    inline void put(McastProdIndex prodIndex, off_t offset);
    /**
     * Removes and returns the offset corresponding to a product-index.
     * @param[in] prodIndex       The product-index.
     * @return                    The corresponding offset.
     * @throws std::out_of_range if the corresponding offset doesn't exist.
     */
    inline off_t get(McastProdIndex prodIndex);

private:
    typedef struct {
        off_t           offset;
        struct timeval  added;
    }                              Element;
    typedef std::mutex             Mutex;
    typedef std::lock_guard<Mutex> Guard;

    std::unordered_map<McastProdIndex, Element> map;
    Mutex                                       mutex;
};

#endif

typedef void OffMap;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a new offset-map.
 * @retval NULL  Failure. `log_add()` called.
 * @return       A new offset-map. The caller should call `om_free()` when
 *               it's no longer needed.
 */
OffMap* om_new();
/**
 * Frees an offset-map.
 * @param[in] offMap  The offset-map to be freed.
 */
void om_free(OffMap* offMap);
/**
 * Adds an entry from a product-index to an offset to an offset-map.
 * @param[in] offMap     The offset map.
 * @param[in] prodIndex  The product-index.
 * @param[in] offset     The offset.
 * @retval 0             If successful.
 * @retval LDM7_SYSTEM   If a system error occurred. `log_add()` called.
 */
int om_put(OffMap* offMap, McastProdIndex prodIndex, off_t offset);
/**
 * Removes and returns the offset corresponding to a product-index from an
 * offset-map.
 * @param[in]  offMap     The offset map.
 * @param[in]  prodIndex  The product-index.
 * @param[out] offset     The corresponding offset.
 * @retval 0              if successful.
 * @retval LDM7_INVAL     if the corresponding offset doesn't exit.
 */
int om_get(OffMap* offMap, McastProdIndex prodIndex, off_t* offset);

#ifdef __cplusplus
}
#endif

#endif /* MLDM_SENDER_OFFSETMAP_H_ */

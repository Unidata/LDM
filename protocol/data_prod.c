/**
 * Copyright 2013 University Corporation for Atmospheric Research.
 * See file ../COPYRIGHT for copying and redistribution conditions.
 * <p>
 * This file contains the data-product abstraction.
 *
 * Created on: Feb 4, 2013
 *     Author: Steven R. Emmerson
 */

#include "config.h"
#include "ldm.h"
#include "data_prod.h"
#include "prod_info.h"


/**
 * The nil data-product. Relies on the fact that static initialization of all
 * members of the data-product structure results in a nil data-product.
 */
static const product    nilProd;


/**
 * Returns the nil data-product.
 *
 * @return  The nil data-product.
 */
const product* dp_getNil(
        void)
{
    return &nilProd;
}


/**
 * Indicates if two data-products are equal.
 *
 * @param prod1     [in] The first data-product.
 * @param prod2     [in] The second data-product.
 * @retval 0        The data-products are unequal.
 * @retval 1        The data-products are equal.
 */
int dp_equals(
        const product* const prod1,
        const product* const prod2)
{
    if (prod1 != prod2) {
        const prod_info* const info1 = &prod1->info;
        const prod_info* const info2 = &prod2->info;

        if (!pi_equals(info1, info2)) {
            return 0;
        }
        else {
            const unsigned  size = pi_getSize(info1);

            if (size > 0) {
                const void* data1 = prod1->data;
                const void* data2 = prod2->data;

                if (data1 != data2) {
                    if (memcmp(data1, data2, size))
                        return 0;
                }
            }
        }
    }

    return 1;
}


/**
 * Indicates if a data-product is the nil data-product.
 *
 * @param prod      [in] The data-product to be checked.
 * @retval 0        The data-product is not the nil data-product.
 * @retval 1        The data-product is the nil data-product.
 */
int dp_isNil(
        const product* const prod)
{
    return dp_equals(prod, &nilProd);
}

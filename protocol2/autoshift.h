#ifndef AUTOSHIFT_H
#define AUTOSHIFT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resets this module. Starts the clock on measuring performance.
 *
 * NB: The number of LDM processes receiving the same data is not modified.
 *
 * @param isPrimary     Whether or not the transmission-mode is primary (i.e.,
 *                      uses HEREIS rather than COMINGSOON/BLKDATA messages)
 * @retval NULL         Success
 */
void
as_init(
    const int	isPrimary);


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
    unsigned    count);


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
    const size_t        size);

/**
 * Indicates whether or not this LDM process should switch its data-product
 * receive-mode. Always returns 0 if "as_setLdmCount()" has not been called.
 *
 * @retval 0       Don't switch
 * @retval 1       Do switch
 */
int
as_shouldSwitch(void);

#ifdef __cplusplus
}
#endif

#endif

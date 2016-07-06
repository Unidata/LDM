/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */
#ifndef _PALT_H_
#define _PALT_H_

#include "pq.h"
#include "timestamp.h"

/*
 * When last successfully-processed data-product was inserted into
 * product-queue:
 */
extern timestampt palt_last_insertion;

#ifdef __cplusplus
extern "C" int readPatFile(const char *path);
extern "C" int processProduct(const prod_info *infop, const void *datap,
	const void *xprod, size_t len,
	void *otherargs);
extern "C" void dummyprod(char *ident);
#elif defined(__STDC__)
extern int readPatFile(const char *path);

#if 0
/**
 * Loop thru the pattern / action table, applying actions
 *
 * @param[in] infop      Pointer to data-product metadata.
 * @param[in] datap      Pointer to data-product data.
 * @param[in] xprod      Pointer to XDR-encoded data-product.
 * @param[in] xlen       Size of XDR-encoded data-product in bytes.
 * @param[in] otherargs  Pointer to optional boolean argument.
 * @retval    0          Success. No error occurred. `*(bool*)otherargs` is
 *                       set to `true` if and only if the data-product was
 *                       successfully processed.
 * @retval    -1         The data-product was not processed because it either
 *                       didn't match any entries or couldn't be processed.
 */
extern int processProduct(const prod_info *infop, const void *datap,
	void *xprod, size_t len,
	void *otherargs);
#else
/**
 * Loop thru the pattern / action table, applying actions to matching product.
 *
 * @param[in] prod_par   Data-product parameters
 * @param[in] queue_par  Product-queue parameters
 * @param[in] noError    Pointer to boolean argument indicating that no error
 *                       occurred while processing data-product
 */
void
processProduct(
        const prod_par_t* const restrict  prod_par,
        const queue_par_t* const restrict queue_par,
        void* const restrict              noError);
#endif
extern void dummyprod(char *ident);
#else /* Old Style C */
extern int readPatFile();
extern int processProduct();
extern void dummyprod();
#endif

#endif /* !_PALT_H_ */

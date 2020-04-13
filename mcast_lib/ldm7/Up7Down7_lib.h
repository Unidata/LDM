/*
 * Library for Up7Down7_test and Down7_test.
 *
 *  Created on: Apr 2, 2020
 *      Author: steve
 */

#ifndef MCAST_LIB_LDM7_UP7DOWN7_LIB_H_
#define MCAST_LIB_LDM7_UP7DOWN7_LIB_H_

#include "ldm.h"

/*
 * Proportion of data-products that the receiving LDM-7 will delete from the
 * product-queue and request from the sending LDM-7 to simulate network
 * problems.
 */
#define RUN_REQUESTER          0
#define REQUEST_RATE           0.1 // !RUN_REQUESTER => Ignored
// Total number of products to insert
#define NUM_PRODS              100
// Maximum size of a data-product in bytes
#define MAX_PROD_SIZE          1000000
// Approximate number of times the product-queue will be "filled".
#define NUM_TIMES              2
// Duration, in microseconds, before the next product is inserted (i.e., gap
// duration)
#define INTER_PRODUCT_GAP      10000
/*
 * Mean residence-time, in seconds, of a data-product. Also used to compute the
 * FMTP retransmission timeout.
 */
#define UP7_HOST               "127.0.0.1"
#define UP7_PORT               3880
#define UP7_PQ_PATHNAME        "up7_test.pq"
#define DOWN7_PQ_PATHNAME      "down7_test.pq"

// Derived values:

// Mean product size in bytes
#define MEAN_PROD_SIZE         (MAX_PROD_SIZE/2)

/*
 * The product-queue is limited by its data-capacity (rather than its product-
 * capacity) to attempt to reproduce the queue corruption seen by Shawn Chen at
 * the University of Virginia.
 */
// Capacity of the product-queue in number of products
#define NUM_SLOTS              (NUM_PRODS/NUM_TIMES)
// Capacity of the product-queue in bytes
#define PQ_DATA_CAPACITY       (NUM_SLOTS*MEAN_PROD_SIZE)

VcEndPoint*   localVcEnd;

void ud7_init(void (*sigHandler)(int sig));
void ud7_free();

#endif /* MCAST_LIB_LDM7_UP7DOWN7_LIB_H_ */

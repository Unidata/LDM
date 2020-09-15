/**
 * SSL helper functions.
 *
 *        File: SslHelp.h
 *  Created on: Sep 4, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_

namespace SslHelp {

/**
 * Initializes the OpenSSL pseudo-random number generator (PRNG).
 *
 * @param[in] numBytes         Number of bytes from "/dev/random" to initialize
 *                             the PRNG with
 * @throws std::system_error   Couldn't open "/dev/random"
 * @throws std::system_error   `read(2)` failure
 * @throws std::runtime_error  `RAND_bytes()` failure
 */
void initRand(const int numBytes);

} // Namespace

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_ */

/**
 * SSL helper functions.
 *
 *        File: SslHelp.h
 *  Created on: Sep 4, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_

#include <queue>
#include <string>

namespace SslHelp {

using OpenSslErrCode = unsigned long;
using CodeQ          = std::queue<OpenSslErrCode>;

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

/**
 * Throws a queue of OpenSSL errors as a nested exception. If the queue is
 * empty, then simply returns. Recursive.
 *
 * @param[in,out] codeQ         Queue of OpenSSL error codes. Will be empty
 *                              on return.
 * @throw std::runtime_error    Earliest OpenSSL error
 * @throw std::nested_exception Nested OpenSSL runtime exceptions
 */
void throwExcept(CodeQ& codeQ);

/**
 * Throws an OpenSSL error. If a current OpenSSL error exists, then it is
 * thrown as a nested exception; otherwise, a regular exception is thrown.
 *
 * @param msg                     Top-level (non-OpenSSL) message
 * @throw std::runtime_exception  Regular or nested exception
 */
void throwOpenSslError(const std::string& msg);

} // Namespace

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SSLHELP_H_ */

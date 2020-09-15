/**
 * Hash-based message authentication code (HMAC).
 *
 *        File: hmac.cpp
 *  Created on: Sep 4, 2020
 *      Author: steve
 */

#include "fmtpBase.h"
#include "hmac.h"
#include "SslHelp.h"

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cassert>
#include <stdexcept>

/**
 * Vets the size of the HMAC key.
 *
 * @param[in] key                 HMAC key
 * @throws std::invalid_argument  `key.size() < 2*SIZE`
 */
static void vetKeySize(const std::string& key)
{
	if (key.size() < 2*Hmac::SIZE) // Double hash size => more secure
		throw std::invalid_argument("key.size()=" + std::to_string(key.size()));
}

/**
 * Creates an EVP_PKEY.
 *
 * @param[in] key             HMAC key
 * @return                    Pointer to corresponding EVP_PKEY. Caller should
 *                            call `EVP_PKEY_free()` when it's no longer needed.
 * @throw std::runtime_error  OpenSSL failure
 */
static EVP_PKEY* createPkey(const std::string& key)
{
	vetKeySize(key);

	EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL,
			reinterpret_cast<const unsigned char*>(key.data()), key.size());
	if (pkey == nullptr)
		throw std::runtime_error("EVP_PKEY_new_raw_private_key() failure. "
				"Code=" + std::to_string(ERR_get_error()));

	return pkey;
}

/**
 * Creates a message-digest context.
 *
 * @return                    Pointer to a message-digest context. Call should
 *                            call `EVP_MD_CTX_free()` when it's no longer
 *                            needed.
 * @throw std::runtime_error  OpenSSL failure
 */
static EVP_MD_CTX* createMdCtx()
{
	auto mdCtx = EVP_MD_CTX_new();
	if (mdCtx == NULL)
		throw std::runtime_error("EVP_MD_CTX_new() failure. "
				"Code=" + std::to_string(ERR_get_error()));
	return mdCtx;
}

/**
 * Initializes this instance.
 *
 * @param[in] key                HMAC key
 * @throw std::invalid_argument  `key.size() < 2*SIZE`
 * @throw std::runtime_error     OpenSSL failure
 */
void Hmac::init(const std::string& key)
{
	vetKeySize(key);
	this->key = key;
	pkey = createPkey(key);
	mdCtx = createMdCtx();
}

Hmac::Hmac()
    : key{}
	, pkey{nullptr}
	, mdCtx{nullptr}
{
	unsigned char bytes[2*SIZE];

	SslHelp::initRand(sizeof(bytes));
	if (RAND_bytes(bytes, sizeof(bytes)) == 0)
		throw std::runtime_error("RAND_bytes() failure. "
				"Code=" + std::to_string(ERR_get_error()));

	init(std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes)));
}

Hmac::Hmac(const std::string& key)
    : key{}
	, pkey{nullptr}
	, mdCtx{nullptr}
{
	init(key);
}

Hmac::~Hmac()
{
	EVP_MD_CTX_free(mdCtx);
	EVP_PKEY_free(pkey);
}

Hmac& Hmac::operator=(Hmac&& rhs)
{
	EVP_MD_CTX_free(mdCtx);
	EVP_PKEY_free(pkey);
	init(rhs.key); // Won't throw because `rhs` passed creation
	return *this;
}

std::string Hmac::getHmac(
		const struct iovec* iov,
		const unsigned      nvec)
{
	if (nvec && iov == nullptr)
		throw std::logic_error("Null I/O vector");

	if (EVP_DigestSignInit(mdCtx, NULL, EVP_sha256(), NULL, pkey) != 1)
		throw std::runtime_error("EVP_DigestSignInit() failure. "
				"Code=" + std::to_string(ERR_get_error()));

	for (const struct iovec* out = iov + nvec; iov < out; ++iov)
		if (!EVP_DigestSignUpdate(mdCtx, iov->iov_base,  iov->iov_len))
			throw std::runtime_error("EVP_DigestUpdate() failure. Code=" +
					std::to_string(ERR_get_error()));

	unsigned char hmacBytes[SIZE];
	size_t        nbytes = sizeof(hmacBytes);
	if (!EVP_DigestSignFinal(mdCtx, hmacBytes, &nbytes))
		throw std::runtime_error("EVP_DigestSignFinal() failure. Code=" +
				std::to_string(ERR_get_error()));
	assert(SIZE == nbytes);

	return std::string(reinterpret_cast<const char*>(hmacBytes), nbytes);
}

std::string Hmac::getHmac(
		const FmtpHeader& header,
		const void*       payload)
{
	if (EVP_DigestSignInit(mdCtx, NULL, EVP_sha256(), NULL, pkey) != 1)
		throw std::runtime_error("EVP_DigestSignInit() failure. "
				"Code=" + std::to_string(ERR_get_error()));

	if (    !EVP_DigestSignUpdate(mdCtx, &header.prodindex,  sizeof(header.prodindex)) ||
			!EVP_DigestSignUpdate(mdCtx, &header.seqnum,     sizeof(header.seqnum)) ||
			!EVP_DigestSignUpdate(mdCtx, &header.payloadlen, sizeof(header.payloadlen)) ||
			!EVP_DigestSignUpdate(mdCtx, &header.flags,      sizeof(header.flags)) ||
			(payload && header.payloadlen > 0 && !EVP_DigestSignUpdate(mdCtx,
				payload, sizeof(header.prodindex))))
		throw std::runtime_error("EVP_DigestUpdate() failure. Code=" +
				std::to_string(ERR_get_error()));

	unsigned char hmacBytes[SIZE];
	size_t        nbytes = sizeof(hmacBytes);
	if (!EVP_DigestSignFinal(mdCtx, hmacBytes, &nbytes))
		throw std::runtime_error("EVP_DigestSignFinal() failure. Code=" +
				std::to_string(ERR_get_error()));
	assert(SIZE == nbytes);

	return std::string(reinterpret_cast<const char*>(hmacBytes), nbytes);
}

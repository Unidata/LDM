/**
 * Hash-based message authentication code (HMAC).
 *
 *        File: HmacImpl.cpp
 *  Created on: Sep 4, 2020
 *      Author: Steven R. Emmerson
 */
#include "FmtpConfig.h"

#include "HmacImpl.h"

#include "fmtpBase.h"
#include "SslHelp.h"

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#if !HAVE_DECL_EVP_PKEY_NEW_RAW_PRIVATE_KEY
    extern "C" EVP_PKEY* EVP_PKEY_new_raw_private_key(
            int                  type,
            ENGINE*              e,
            const unsigned char* priv,
            size_t               len);
#endif
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>

/**
 * Vets the size of the HMAC key.
 *
 * @param[in] key                 HMAC key
 * @throws std::invalid_argument  `key.size() < 2*SIZE`
 */
static void vetKeySize(const std::string& key)
{
    if (key.size() < 2*MAC_SIZE) // Double hash size => more secure
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
 * Assigns members `key`, `pkey`, and `mdCtx` after vetting the size of the
 * HMAC key.
 *
 * @param[in] key                HMAC key
 * @throw std::invalid_argument  `key.size() < 2*SIZE`
 * @throw std::runtime_error     OpenSSL failure
 */
void HmacImpl::init(const std::string& key)
{
    vetKeySize(key);
    this->key = key;
    pkey = createPkey(key);
    mdCtx = createMdCtx();
}

HmacImpl::HmacImpl()
    : key{}
    , pkey{nullptr}
    , mdCtx{nullptr}
    , pkcKey{new PrivateKey()}
{
    unsigned char bytes[2*MAC_SIZE];

    SslHelp::initRand(sizeof(bytes));
    if (RAND_bytes(bytes, sizeof(bytes)) == 0)
        throw std::runtime_error("RAND_bytes() failure. "
                "Code=" + std::to_string(ERR_get_error()));

    init(std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes)));
}

HmacImpl::HmacImpl(const std::string& key)
    : key{}
    , pkey{nullptr}
    , mdCtx{nullptr}
    , pkcKey{nullptr}
{
    init(key);
    //pkcKey = new PublicKey();
}

HmacImpl::~HmacImpl()
{
    delete pkcKey;
    if (mdCtx)
        EVP_MD_CTX_free(mdCtx);
    if (pkey)
        EVP_PKEY_free(pkey);
}

HmacImpl& HmacImpl::operator=(HmacImpl&& rhs)
{
    this->key = std::move(rhs.key);

    EVP_PKEY_free(pkey);
    pkey = std::move(rhs.pkey);
    rhs.pkey = nullptr;

    EVP_MD_CTX_free(mdCtx);
    mdCtx = std::move(rhs.mdCtx);
    rhs.mdCtx = nullptr;

    return *this;
}

void HmacImpl::getMac(const FmtpHeader& header,
                      const void*       payload,
                      char              mac[MAC_SIZE])
{
    if (header.payloadlen && payload == nullptr)
        throw std::logic_error("Inconsistent header and payload");

    if (EVP_DigestSignInit(mdCtx, NULL, EVP_sha256(), NULL, pkey) != 1)
        throw std::runtime_error("EVP_DigestSignInit() failure. "
                "Code=" + std::to_string(ERR_get_error()));

    if (!EVP_DigestSignUpdate(mdCtx, &header,  sizeof(header)) ||
            (header.payloadlen > 0 && !EVP_DigestSignUpdate(mdCtx,
                payload, header.payloadlen)))
        throw std::runtime_error("EVP_DigestUpdate() failure. Code=" +
                std::to_string(ERR_get_error()));

    size_t nbytes = MAC_SIZE;
    if (!EVP_DigestSignFinal(mdCtx, reinterpret_cast<unsigned char*>(mac),
            &nbytes))
        throw std::runtime_error("EVP_DigestSignFinal() failure. Code=" +
                std::to_string(ERR_get_error()));
    assert(MAC_SIZE == nbytes);
}

std::string HmacImpl::to_string(const char mac[MAC_SIZE])
{
    char  buf[2+2*MAC_SIZE+1] = "0x";
    char* cp = buf+2;

    for (int i = 0; i < MAC_SIZE; ++i) {
        ::sprintf(cp, "%02x", mac[i]);
        cp += 2;
    }

    return std::string(buf);
}

bool HmacImpl::isDisabled()
{
    auto value = ::getenv("DISABLE_HMAC");
    return value != NULL && (
            ::strcasecmp(value, "1") == 0 ||
            ::strcasecmp(value, "yes") == 0 ||
            ::strcasecmp(value, "true") == 0);
}

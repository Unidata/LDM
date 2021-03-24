/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      DigSig.cpp
 * @author    Steven R. Emmerson <emmerson@ucar.edu>
 * @version   1.0
 * @date      Mar 8, 2021
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Digital signature module based on Ed25519
 */
#include "FmtpConfig.h"

#include "Ed25519.h"
#include "SslHelp.h"

#include <cassert>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <stdexcept>

/**
 * The twisted Edwards curve digital signing algorithm, Ed25519, is chosen
 * because it 1) is fast; 2) has a 128 bit security level; and 3) has a
 * fixed-length, 64-byte signature.
 */

Ed25519::Ed25519()
    : pKey{nullptr}
    , mdCtx{EVP_MD_CTX_new()}
    , pubKey{}
{
    if (mdCtx == nullptr)
        SslHelp::throwOpenSslError("EVP_MD_CTX_new() failure");

    EVP_PKEY_CTX* pKeyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (pKeyCtx == nullptr)
        SslHelp::throwOpenSslError("EVP_PKEY_CTX_new_id() failure");

    try {
        if (EVP_PKEY_keygen_init(pKeyCtx) != 1)
            SslHelp::throwOpenSslError("EVP_PKEY_keygen_init() failure");

        if (EVP_PKEY_keygen(pKeyCtx, &pKey) != 1)
            SslHelp::throwOpenSslError("EVP_PKEY_keygen() failure");

        EVP_PKEY_CTX_free(pKeyCtx);

        BIO* bio = ::BIO_new(BIO_s_mem());
        if (bio == nullptr)
            SslHelp::throwOpenSslError("BIO_new() failure");

        try {
            /*
             * Apparently, all the following steps are necessary to obtain a
             * std::string with a printable public key.
             */
            if (::PEM_write_bio_PUBKEY(bio, pKey) == 0) // Doesn't NUL-terminate
                SslHelp::throwOpenSslError("PEM_write_bio_PUBKEY() failure");

            const size_t keyLen = BIO_pending(bio);
            char         keyBuf[keyLen]; // NB: No trailing NUL

            if (::BIO_read(bio, keyBuf, keyLen) != keyLen)
                SslHelp::throwOpenSslError("BIO_read() failure");

            ::BIO_free_all(bio);

            // Finally!
            pubKey = std::string(keyBuf, keyLen);
        } // `bio` allocated
        catch (const std::exception& ex) {
            ::BIO_free_all(bio);
            throw;
        }
    } // `pKeyCtx` allocated
    catch (const std::exception& ex) {
        EVP_PKEY_CTX_free(pKeyCtx);
        throw;
    }
}

Ed25519::Ed25519(const std::string& pubKey)
    : pKey{nullptr}
    , mdCtx{EVP_MD_CTX_new()}
    , pubKey{}
{
    BIO* bio = BIO_new_mem_buf(pubKey.data(), pubKey.size());
    if (bio == nullptr)
        SslHelp::throwOpenSslError("BIO_new_mem_buf() failure");

    try {
        PEM_read_bio_PUBKEY(bio, &pKey, nullptr, nullptr);
        if (pKey == nullptr)
            SslHelp::throwOpenSslError("PEM_read_bio_PUBKEY() failure");

        BIO_free_all(bio);

        this->pubKey = pubKey;
    } // `bio` allocated
    catch (const std::exception& ex) {
        BIO_free_all(bio);
        throw;
    }
}

Ed25519::~Ed25519()
{
    EVP_PKEY_free(pKey);
    EVP_MD_CTX_free(mdCtx);
}

std::string Ed25519::getPubKey() const
{
    return pubKey;
}

size_t Ed25519::sign(const char*  msg,
                     const size_t msgLen,
                     char*        sig,
                     const size_t sigLen)
{
    if (!EVP_DigestSignInit(mdCtx, nullptr, nullptr, nullptr, pKey))
         SslHelp::throwOpenSslError("EVP_DigestSignInit() failure");

    size_t len = sigLen;
    if (!EVP_DigestSign(mdCtx,
            reinterpret_cast<unsigned char*>(sig), &len,
            reinterpret_cast<const unsigned char*>(msg), msgLen))
         SslHelp::throwOpenSslError("EVP_DigestSign() failure");

    return len;
}

std::string Ed25519::sign(const std::string& msg)
{
    char         sig[SIGLEN];
    const size_t sigLen = sign(msg.data(), msg.size(), sig, SIGLEN);

    return std::string(sig, sigLen);
}

bool Ed25519::verify(const char*  msg,
                     const size_t msgLen,
                     const char*  sig,
                     const size_t sigLen)
{
    if (!EVP_DigestVerifyInit(mdCtx, nullptr, nullptr, nullptr, pKey))
        SslHelp::throwOpenSslError("EVP_DigestVerifyInit() failure");

    return EVP_DigestVerify(mdCtx,
            reinterpret_cast<const unsigned char*>(sig), sigLen,
            reinterpret_cast<const unsigned char*>(msg), msgLen) == 1;
}

bool Ed25519::verify(const std::string& msg,
                     const std::string& sig)
{
    return verify(msg.data(), msg.size(), sig.data(), sig.size());
}

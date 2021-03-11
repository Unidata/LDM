/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      ecdsa.cpp
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
 * @brief     Elliptic curve digital signing algorithm
 */

#include "FmtpConfig.h"

#include "ecdsa.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <queue>
#include <stdexcept>

Ecdsa::Ecdsa()
{
    EC_builtin_curve curve;

    EC_get_builtin_curves(&curve, 1);
    EC_KEY* ecKey = EC_KEY_new_by_curve_name(curve.nid);
    if (ecKey == nullptr)
        throwOpenSslError("EC_KEY_new_by_curve_name() failure");

    try {
        if (EC_KEY_generate_key(ecKey) == 0)
            throwOpenSslError("EC_KEY_generate_key() failure");
    } // `ecKey` allocated
    catch (const std::exception& ex) {
        EC_KEY_free(ecKey);
        throw;
    }
}

Ecdsa::~Ecdsa()
{
    EC_KEY_free(ecKey);
}

void Ecdsa::throwExcept(CodeQ& codeQ)
{
    if (!codeQ.empty()) {
        OpenSslErrCode code = codeQ.front();
        codeQ.pop();

        try {
            throwExcept(codeQ);
            throw std::runtime_error(ERR_reason_error_string(code));
        }
        catch (const std::exception& ex) {
            std::throw_with_nested(std::runtime_error(
                    ERR_reason_error_string(code)));
        }
    }
}

void Ecdsa::throwOpenSslError(const std::string& msg)
{
    CodeQ codeQ;

    for (OpenSslErrCode code = ERR_get_error(); code; code = ERR_get_error())
        codeQ.push(code);

    try {
        throwExcept(codeQ);
        throw std::runtime_error(msg);
    }
    catch (const std::runtime_error& ex) {
        std::throw_with_nested(std::runtime_error(msg));
    }
}

EcdsaSigner::EcdsaSigner()
    : Ecdsa{}
{
    EC_builtin_curve curve;
    EC_get_builtin_curves(&curve, 1);
    ::printf("Curve name=\"%s\"\n", curve.comment);

    EC_KEY* ecKey = EC_KEY_new_by_curve_name(curve.nid);
    if (ecKey == nullptr)
        throwOpenSslError("EC_KEY_new_by_curve_name() failure");

    try {
        pKey = EVP_PKEY_new();
        if (pKey == nullptr)
            throwOpenSslError("EVP_PKEY_new() failure");

        try {
            /*
             * According to
             * <https://www.openssl.org/docs/man1.0.2/man3/EVP_PKEY_set1_RSA.html>,
             * `ecKey` will not be freed by `EVP_KEY_free()`
             */
            if (!EVP_PKEY_set1_EC_KEY(pKey, ecKey))
                throwOpenSslError("EVP_PKEY_set1_EC_KEY() failure");
        } // `pKey` allocated
        catch (const std::exception& ex) {
            EVP_PKEY_free(pKey); // Doesn't free `ecKey`
            throw;
        }
    } // `ecKey` allocated
    catch (const std::exception& ex) {
        EC_KEY_free(ecKey);
        throw;
    }
}

EcdsaSigner::~EcdsaSigner()
{
    EC_KEY* ecKey = EVP_PKEY_get1_EC_KEY(pKey);
    EVP_PKEY_free(pKey); // Doesn't free `ecKey`
    EC_KEY_free(ecKey);
}

void EcdsaSigner::sign(const std::string& message,
                       std::string&       signature)
{
    //throw std::runtime_error("Unimplemented");
#if 1
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new(); // Message digest context

    if (mdctx == 0)
        throwOpenSslError("EVP_MD_CTX_new() failure: ");

    /*
     * Initialize the DigestSign operation. The SHA-256 message digest function
     * has a security level of 128 bits.
     */
     if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pKey))
         throwOpenSslError("EVP_DigestSignInit() failure");

     /* Call update with the message */
     if (!EVP_DigestSignUpdate(mdctx, static_cast<void*>(message.data()),
             message.length()))
         throwOpenSslError("EVP_DigestSignUpdate() failure");

     /* Finalise the DigestSign operation */
     /* First call EVP_DigestSignFinal with a NULL sig parameter to obtain the length of the
      * signature. Length is returned in slen */
     if(1 != EVP_DigestSignFinal(mdctx, NULL, slen)) goto err;
     /* Allocate memory for the signature based on size in slen */
     if(!(*sig = OPENSSL_malloc(sizeof(unsigned char) * (*slen)))) goto err;
     /* Obtain the signature */
     if(1 != EVP_DigestSignFinal(mdctx, *sig, slen)) goto err;

     /* Success */
     ret = 1;

     err:
     if(ret != 1)
     {
       /* Do some error handling */
     }

     /* Clean up */
     if(*sig && !ret) OPENSSL_free(*sig);
     if(mdctx) EVP_MD_CTX_destroy(mdctx);
#endif

#if 0
    ECDSA_SIG *signature = ECDSA_do_sign(hash, strlen(hash), eckey);
    if (NULL == signature)
    {
        printf("Failed to generate EC Signature\n");
        status = -1;
    }
    else
    {

        int verify_status = ECDSA_do_verify(hash, strlen(hash), signature, eckey);
        const int verify_success = 1;
        if (verify_success != verify_status)
        {
            printf("Failed to verify EC Signature\n");
            status = -1;
        }
        else
        {
            printf("Verifed EC Signature\n");
            status = 1;
        }
    }
#endif
}

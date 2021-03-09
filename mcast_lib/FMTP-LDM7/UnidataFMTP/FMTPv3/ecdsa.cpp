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

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <stdexcept>

Ecdsa::Ecdsa()
{}

Ecdsa::~Ecdsa()
{
    EC_builtin_curve curve;

    EC_get_builtin_curves(&curve, 1);
    EC_KEY* ecKey = EC_KEY_new_by_curve_name(curve.nid);
    if (ecKey == nullptr)
        throw std::runtime_error("EC_KEY_new_by_curve_name() failure");

    try {
        if (EC_KEY_generate_key(ecKey) == 0)
            throw std::runtime_error("EC_KEY_generate_key() failure");
    } // `ecKey` allocated
    catch (const std::exception& ex) {
        EC_KEY_free(ecKey);
        throw;
    }
}

#if 0
    throw std::runtime_error("EVP_MD_CTX_new() failure: " +
            std::string(ERR_get_error()))

void Ecdsa::throwExcept(const std::string& msg) const
{
    throw openssl
            std::string(ERR_get_error()))
}
#endif

EcdsaSigner::EcdsaSigner()
    : Ecdsa{}
{}

void EcdsaSigner::sign(const std::string& message,
                       std::string&       signature)
{
#if 0
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new(); // Message digest context

    if (mdctx == 0)
        throw std::runtime_error("EVP_MD_CTX_new() failure: " +
                std::string(ERR_get_error()))

    /* Initialise the DigestSign operation - SHA-256 has been selected as the message digest function in this example */
     if(1 != EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, key)) goto err;

     /* Call update with the message */
     if(1 != EVP_DigestSignUpdate(mdctx, msg, strlen(msg))) goto err;

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

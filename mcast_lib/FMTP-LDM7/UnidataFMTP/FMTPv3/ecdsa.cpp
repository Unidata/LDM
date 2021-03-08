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

Ecdsa::Ecdsa()
{}

Ecdsa::~Ecdsa()
{
    int status = -1;
    EC_KEY *eckey=EC_KEY_new_by_curve_name(NID_);
    if (NULL == eckey)
    {
        printf("Failed to create new EC Key\n");
        status = -1;
    }
    else
    {
        EC_GROUP *ecgroup= EC_GROUP_new_by_curve_name(NID_secp192k1);
        if (NULL == ecgroup)
        {
            printf("Failed to create new EC Group\n");
            status = -1;
        }
        else
        {
            int set_group_status = EC_KEY_set_group(eckey,ecgroup);
            const int set_group_success = 1;
            if (set_group_success != set_group_status)
            {
                printf("Failed to set group for EC Key\n");
                status = -1;
            }
            else
            {
                const int gen_success = 1;
                int gen_status = EC_KEY_generate_key(eckey);
                if (gen_success != gen_status)
                {
                    printf("Failed to generate EC Key\n");
                    status = -1;
                }
                else
                {
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
                }
            }
            EC_GROUP_free(ecgroup);
        }
        EC_KEY_free(eckey);
    }

  return status;

}

EcdsaSigner::EcdsaSigner()
    : Ecdsa{}
{

}

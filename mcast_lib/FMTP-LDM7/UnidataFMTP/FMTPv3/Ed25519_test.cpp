/**
 * This file tests class `DigSig`.
 *
 * Copyright 2021 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: ecdsa_test2.cpp
 * Created On: Mar 9, 2021
 *     Author: Steven R. Emmerson
 */
#include "FmtpConfig.h"

#include "Ed25519.h"

#include <cstdio>
#include <gtest/gtest.h>

namespace {

/// The fixture for testing class `DigSig`
class Ed25519Test : public ::testing::Test
{
protected:
    char        msgBuf[1420];
    std::string msgStr;

    Ed25519Test()
    {
        for (int i = 0; i < sizeof(msgBuf); ++i)
            msgBuf[i] = i;

        msgStr = std::string(msgBuf, sizeof(msgBuf));
    }

    virtual ~Ed25519Test() =default;
};

// Tests default signer construction
TEST_F(Ed25519Test, DefaultSignerConstruction)
{
    Ed25519();
}

// Tests signing a message buffer
TEST_F(Ed25519Test, SigningBuffer)
{
    Ed25519     signer{};
    char        signature[Ed25519::MAX_SIGLEN];
    size_t      sigLen = signer.sign(msgBuf, sizeof(msgBuf), signature,
            sizeof(signature));

    std::cerr << "Signature length=" << sigLen << '\n';
    std::cerr << "Signature=0x";
    for (int i = 0; i < sigLen; ++i)
        ::fprintf(stderr, "%02x", signature[i]);
    std::cerr << '\n';
}

// Tests signing a message string
TEST_F(Ed25519Test, SigningString)
{
    Ed25519      signer{};
    std::string signature = signer.sign(msgStr);

    std::cerr << "Signature length=" << signature.length() << '\n';
    std::cerr << "Signature=0x";
    for (unsigned char c : signature)
        ::fprintf(stderr, "%02x", c);
    std::cerr << '\n';
}

// Tests getting a signer's public key
TEST_F(Ed25519Test, PublicKey)
{
    Ed25519 signer{};
    std::string pubKey = signer.getPubKey();
    std::cerr << pubKey;
}

// Tests default verifier construction
TEST_F(Ed25519Test, DefaultVerifierConstruction)
{
    Ed25519 signer{};
    std::string pubKey = signer.getPubKey();

    Ed25519{pubKey};
}

// Tests verifying a message buffer
TEST_F(Ed25519Test, VerifySignatureBuffer)
{
    Ed25519      signer{};
    std::string pubKey = signer.getPubKey();
    char        signature[Ed25519::MAX_SIGLEN];
    size_t      sigLen = signer.sign(msgBuf, sizeof(msgBuf), signature,
            sizeof(signature));

    Ed25519 verifier{pubKey};
    EXPECT_TRUE(verifier.verify(msgBuf, sizeof(msgBuf), signature, sigLen));

    msgBuf[0] ^= 1; // Flip one bit
    EXPECT_FALSE(verifier.verify(msgBuf, sizeof(msgBuf), signature, sigLen));
    msgBuf[0] ^= 1; // Restore bit

    signature[0] ^= 1; // Flip one bit
    EXPECT_FALSE(verifier.verify(msgBuf, sizeof(msgBuf), signature, sigLen));
}

// Tests verifying a message string
TEST_F(Ed25519Test, VerifySignatureString)
{
    Ed25519      signer{};
    std::string pubKey = signer.getPubKey();

    std::string signature = signer.sign(msgStr);

    Ed25519 verifier{pubKey};
    EXPECT_TRUE(verifier.verify(msgStr, signature));

    msgStr[0] ^= 1; // Flip one bit
    EXPECT_FALSE(verifier.verify(msgStr, signature));
    msgStr[0] ^= 1; // Restore bit

    signature[0] ^= 1; // Flip one bit
    EXPECT_FALSE(verifier.verify(msgStr, signature));
}

// Tests verifying a non-continuous sequence of messages
TEST_F(Ed25519Test, VerifySignatureSequence)
{
    Ed25519      signer{};
    std::string pubKey = signer.getPubKey();

    Ed25519      verifier{pubKey};

    for (int i = 0; i < 3; ++i) {
        char   signature[Ed25519::MAX_SIGLEN];

        msgBuf[0] = i;

        size_t sigLen = signer.sign(msgBuf, sizeof(msgBuf), signature,
                sizeof(signature));

        std::cerr << "Signature=0x";
        for (int i = 0; i < sigLen; ++i)
            ::fprintf(stderr, "%02x", signature[i]);
        std::cerr << '\n';

        if (i != 1) // Skip a msgBuf
            EXPECT_TRUE(verifier.verify(msgBuf, sizeof(msgBuf),
                    signature, sigLen));
    }
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * This file tests class `Mac`.
 *
 * Copyright 2021 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: mac_test2.cpp
 * Created On: Mar 9, 2021
 *     Author: Steven R. Emmerson
 */
#include "FmtpConfig.h"

#include "mac.h"

#include <cstdio>
#include <gtest/gtest.h>

namespace {

/// The fixture for testing class `Mac`
class MacTest : public ::testing::Test
{
protected:
    char        msgBuf[1420];
    std::string msgStr;

    MacTest()
    {
        for (int i = 0; i < sizeof(msgBuf); ++i)
            msgBuf[i] = i;

        msgStr = std::string(msgBuf, sizeof(msgBuf));
    }

    virtual ~MacTest() =default;
};

// Tests no MAC
TEST_F(MacTest, NoMac)
{
    ::setenv(Mac::ENV_NAME, "0", 1);
    Mac signer{};
    auto mac = signer.getMac(msgStr);
    ASSERT_EQ(0, mac.size());
    Mac verifier{signer.getKey()};
    ASSERT_TRUE(verifier.verify(msgStr, mac));
}

// Tests HMAC
TEST_F(MacTest, HMAC)
{
    ::setenv(Mac::ENV_NAME, "1", 1);
    Mac signer{};
    Mac verifier{signer.getKey()};
    for (int i = 0; i < 3; ++i) {
        msgStr[0] = i;
        auto mac = signer.getMac(msgStr);
        ASSERT_EQ(32, mac.size());
        if (i != 1) // Tests non-contiguity
            ASSERT_TRUE(verifier.verify(msgStr, mac));
    }
}

// Tests digital signature algorithm
TEST_F(MacTest, DSA)
{
    ::setenv(Mac::ENV_NAME, "2", 1);
    Mac signer{};
    Mac verifier{signer.getKey()};
    for (int i = 0; i < 3; ++i) {
        msgStr[0] = i;
        auto mac = signer.getMac(msgStr);
        ASSERT_EQ(64, mac.size());
        if (i != 1) // Tests non-contiguity
            ASSERT_TRUE(verifier.verify(msgStr, mac));
    }
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

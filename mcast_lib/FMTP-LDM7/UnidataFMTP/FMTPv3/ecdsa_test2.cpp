/**
 * This file tests class `Ecdsa`.
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

#include "ecdsa.h"

#include "gtest/gtest.h"

namespace {

/// The fixture for testing class `Ecdsa`
class EcdsaTest : public ::testing::Test
{
protected:
    // You can remove any or all of the following functions if its body
    // is empty.

    EcdsaTest()
    {
        // You can do set-up work for each test here.
    }

    virtual ~EcdsaTest()
    {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    virtual void SetUp()
    {
        // Code here will be called immediately after the constructor (right
        // before each test).
    }

    virtual void TearDown()
    {
        // Code here will be called immediately after each test (right
        // before the destructor).
    }

    // Objects declared here can be used by all tests in the test case for Error.
};

// Tests default construction
TEST_F(EcdsaTest, DefaultSignerConstruction)
{
    EcdsaSigner();
}

// Tests signing a message
TEST_F(EcdsaTest, Signing)
{
    EcdsaSigner signer;
    std::string signature;
    signer.sign("Hello, world!", signature);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

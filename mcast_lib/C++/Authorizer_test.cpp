/**
 * This file tests class `Authorizer`.
 *
 * Copyright 2017 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: Authorizer_test.cpp
 * Created On: Feb 6, 2018
 *     Author: Steven R. Emmerson
 */
#include "config.h"

#include "Authorizer.h"

#include "gtest/gtest.h"

namespace {

/// The fixture for testing class `Authorizer`
class AuthorizerTest : public ::testing::Test
{
protected:
    // You can remove any or all of the following functions if its body
    // is empty.

    AuthorizerTest()
    {
        // You can do set-up work for each test here.
    }

    virtual ~AuthorizerTest()
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
TEST_F(AuthorizerTest, DefaultConstruction)
{
    Authorizer auth();
}

// Tests authorization
TEST_F(AuthorizerTest, Authorization)
{
    Authorizer auth{};
    struct in_addr inAddr;
    ::inet_pton(AF_INET, "127.0.0.1", &inAddr.s_addr);
    EXPECT_FALSE(auth.isAuthorized(inAddr));
    auth.authorize(inAddr);
    EXPECT_TRUE(auth.isAuthorized(inAddr));
}

// Tests de-authorization
TEST_F(AuthorizerTest, DeAuthorization)
{
    Authorizer auth{};
    struct in_addr inAddr;
    ::inet_pton(AF_INET, "127.0.0.1", &inAddr.s_addr);
    auth.authorize(inAddr);
    EXPECT_TRUE(auth.isAuthorized(inAddr));
    auth.deauthorize(inAddr);
    EXPECT_FALSE(auth.isAuthorized(inAddr));
}

// Tests timeout de-authorization
TEST_F(AuthorizerTest, TimeoutDeAuthorization)
{
    Authorizer auth{1};
    struct in_addr inAddr;
    ::inet_pton(AF_INET, "127.0.0.1", &inAddr.s_addr);
    auth.authorize(inAddr);
    EXPECT_TRUE(auth.isAuthorized(inAddr));
    ::usleep(1100000);
    EXPECT_FALSE(auth.isAuthorized(inAddr));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

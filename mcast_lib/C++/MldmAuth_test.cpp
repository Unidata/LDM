/**
 * This file tests class `MldmAuth`.
 *
 * Copyright 2017 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: MldmAuth_test.cpp
 * Created On: Feb 6, 2018
 *     Author: Steven R. Emmerson
 */
#include "config.h"

#include "MldmAuth.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <thread>

namespace {

/// The fixture for testing class `MldmAuth`
class MldmAuthTest : public ::testing::Test
{};

// Tests default server construction
TEST_F(MldmAuthTest, DefaultConstruction)
{
    Authorizer   authorizer{};
    MldmAuthSrvr mldmAuth(authorizer);
    EXPECT_LT(0, mldmAuth.getPort());
}

// Tests authorizing
TEST_F(MldmAuthTest, Authorizing)
{
    Authorizer   authorizer{};
    MldmAuthSrvr mldmAuth(authorizer);

    std::thread thread{[&mldmAuth] {
        mldmAuth.runServer();
    }};
    thread.detach();

    struct in_addr inAddr;
    ::inet_pton(AF_INET, "128.117.140.56", &inAddr.s_addr);
    EXPECT_FALSE(authorizer.isAuthorized(inAddr));
    auto status = mldmAuth_authorize(mldmAuth.getPort(), inAddr.s_addr);
    EXPECT_EQ(0, status);
    EXPECT_TRUE(authorizer.isAuthorized(inAddr));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

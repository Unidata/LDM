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
{};

// Tests construction
TEST_F(AuthorizerTest, Construction)
{
    in_addr_t addr;
    inet_pton(AF_INET, "192.168.8.0", &addr);
    InAddrPool inAddrPool{addr, 21};
    Authorizer auth(inAddrPool);
}

// Tests authorization
TEST_F(AuthorizerTest, Authorization)
{
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.8.0", &addr);
    InAddrPool inAddrPool{addr.s_addr, 21};
    Authorizer auth(inAddrPool);
    EXPECT_FALSE(auth.isAuthorized(addr));
    addr.s_addr = inAddrPool.reserve();
    EXPECT_TRUE(auth.isAuthorized(addr));
    inAddrPool.release(addr.s_addr);
    EXPECT_FALSE(auth.isAuthorized(addr));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

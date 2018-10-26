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
#include "CidrAddr.h"

#include <gtest/gtest.h>

namespace {

/// The fixture for testing class `Authorizer`
class AuthorizerTest : public ::testing::Test
{};

// Tests construction
TEST_F(AuthorizerTest, Construction)
{
    in_addr_t addr;
    inet_pton(AF_INET, "192.168.8.0", &addr);
    CidrAddr* subnet = cidrAddr_new(addr, 21);
    InAddrPool inAddrPool{*subnet};
    Authorizer auth(inAddrPool, ANY);
    cidrAddr_delete(subnet);
}

// Tests authorization
TEST_F(AuthorizerTest, Authorization)
{
    struct in_addr addr;
    inet_pton(AF_INET, "192.168.8.0", &addr.s_addr);
    CidrAddr* subnet = cidrAddr_new(addr.s_addr, 21);
    InAddrPool inAddrPool{*subnet};
    Authorizer auth(inAddrPool, ANY);
    EXPECT_FALSE(auth.isAuthorized(addr));
    addr.s_addr = inAddrPool.reserve();
    EXPECT_TRUE(auth.isAuthorized(addr));
    inAddrPool.release(addr.s_addr);
    EXPECT_FALSE(auth.isAuthorized(addr));
    cidrAddr_delete(subnet);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * This file tests module `PubKeyCrypt`.
 *
 * Copyright 2021 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: PubKeyCrypt_test.cpp
 * Created On: Mar 5, 2021
 *     Author: Steven R. Emmerson
 */

#include "PubKeyCrypt.h"

#include <cstring>
#include <gtest/gtest.h>
#include <iostream>

namespace {

/// The fixture for testing module `PubKeyCrypt`
class PubKeyCryptTest : public ::testing::Test
{};

// Tests `PubKeyCrypt` on an HMAC
TEST_F(PubKeyCryptTest, Hmac)
{
    EXPECT_EQ(0, 0);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

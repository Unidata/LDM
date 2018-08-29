/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ProdIndexDelayQueue_test.cpp
 * @author: Steven R. Emmerson
 *
 * This file tests class `ProdIndexDelayQueue`.
 */

#include "ProdIndexDelayQueue.h"
#include "gtest/gtest.h"

#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {

// The fixture for testing class ProdIndexDelayQueue.
class ProdIndexDelayQueueTest : public ::testing::Test {
 protected:
  // You can remove any or all of the following functions if its body
  // is empty.

  ProdIndexDelayQueueTest() {
    // You can do set-up work for each test here.
  }

  virtual ~ProdIndexDelayQueueTest() {
    // You can do clean-up work that doesn't throw exceptions here.
  }

  // If the constructor and destructor are not enough for setting up
  // and cleaning up each test, you can define the following methods:

  virtual void SetUp() {
    // Code here will be called immediately after the constructor (right
    // before each test).
  }

  virtual void TearDown() {
    // Code here will be called immediately after each test (right
    // before the destructor).
  }

  // Objects declared here can be used by all tests in the test case for
  // ProdIndexDelayQueue.
  ProdIndexDelayQueue q;

};

TEST_F(ProdIndexDelayQueueTest, ConstructDestruct) {
}

TEST_F(ProdIndexDelayQueueTest, PushElement) {
    q.push(1, 0.1);
    ASSERT_EQ(1, q.size());
}

TEST_F(ProdIndexDelayQueueTest, PopElement) {
    q.push(1, 0.1);
    ASSERT_EQ(1, q.pop());
    ASSERT_EQ(0, q.size());
}

TEST_F(ProdIndexDelayQueueTest, NegativeDuration) {
    q.push(1, 0.5);
    q.push(2, -0.5);
    ASSERT_EQ(2, q.pop());
    ASSERT_EQ(1, q.pop());
    ASSERT_EQ(0, q.size());
}

TEST_F(ProdIndexDelayQueueTest, DisablingCausesPushException) {
    q.disable();
    ASSERT_THROW(q.push(1, 0.5), std::runtime_error);
}

TEST_F(ProdIndexDelayQueueTest, DisablingCausesPopException) {
    q.disable();
    EXPECT_THROW((void)q.pop(), std::runtime_error);
}

TEST_F(ProdIndexDelayQueueTest, Performance) {
    std::default_random_engine             generator;
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    for (int i = 0; i < 10000; i++)
        q.push(i, distribution(generator));
    std::chrono::system_clock::time_point start =
            std::chrono::system_clock::now();
    for (int i = 10000; i < 20000; i++) {
        q.push(i, distribution(generator));
        (void)q.get();
    }
    std::chrono::system_clock::time_point stop =
            std::chrono::system_clock::now();
    ASSERT_EQ(10000, q.size());
    std::chrono::system_clock::duration duration = stop - start;
    double seconds = (std::chrono::duration_cast
            <std::chrono::duration<double>>(duration)).count();
    std::cerr << "10,000 push()/get()s in " << std::to_string(seconds) <<
            " seconds\n";
    std::cerr << std::to_string(10000/seconds) << " s-1\n";
}

#if 0

// Tests that the ProdIndexDelayQueue::Bar() method does Abc.
TEST_F(ProdIndexDelayQueueTest, MethodBarDoesAbc) {
  const string input_filepath = "this/package/testdata/myinputfile.dat";
  const string output_filepath = "this/package/testdata/myoutputfile.dat";
  ProdIndexDelayQueue f;
  EXPECT_EQ(0, f.Bar(input_filepath, output_filepath));
}

// Tests that ProdIndexDelayQueue does Xyz.
TEST_F(ProdIndexDelayQueueTest, DoesXyz) {
  // Exercises the Xyz feature of ProdIndexDelayQueue.
}
#endif

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

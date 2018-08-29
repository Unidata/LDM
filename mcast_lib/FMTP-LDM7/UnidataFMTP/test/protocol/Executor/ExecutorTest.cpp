/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ExecutorTest.cpp
 * @author: Steven R. Emmerson
 *
 * This file tests the Executor, Wip, and Task classes.
 */

#include "Executor.h"
#include <gtest/gtest.h>
#include <pthread.h>
#include <string>
#include <signal.h>
#include <stdexcept>

namespace {

// The fixture for testing class Executor.
class ExecutorTest : public ::testing::Test {
  protected:
      ExecutorTest()
          : one(1),
            two(2),
            terminatingTask1(&one),
            terminatingTask2(&two),
            indefiniteTask1(&one) {
      }

      class TerminatingTask : public Task {
      public:
          TerminatingTask(void* arg) : arg(arg) {};

          void* start()
          {
              sleep(1);
              return arg;
          }

      private:
          void* arg;
      };

      class IndefiniteTask : public Task {
      public:
          IndefiniteTask(void* arg)
              : arg(arg),
                done(0)
          {
                pthread_mutex_init(&mutex, 0);
                pthread_cond_init(&cond, 0);
          };

          ~IndefiniteTask() {
              pthread_mutex_destroy(&mutex);
              pthread_cond_destroy(&cond);
          };

          void* start()
          {
              (void)pthread_mutex_lock(&mutex);
              while(!done)
                  (void)pthread_cond_wait(&cond, &mutex);
              (void)pthread_mutex_unlock(&mutex);
              return arg;
          }

          void stop() {
              (void)pthread_mutex_lock(&mutex);
              done = 1;
              (void)pthread_cond_signal(&cond);
              (void)pthread_mutex_unlock(&mutex);
          }

      private:
          void*                 arg;
          volatile sig_atomic_t done;
          pthread_mutex_t       mutex;
          pthread_cond_t        cond;
      };

      int             one;
      int             two;
      Executor        executor;
      TerminatingTask terminatingTask1;
      TerminatingTask terminatingTask2;
      IndefiniteTask  indefiniteTask1;
};

// Tests that the Executor correctly executes a single, self-terminating task.
TEST_F(ExecutorTest, OneSelfTerminatingTask) {
    Wip* wip = executor.submit(terminatingTask1);
    Wip* doneWip = executor.wait();

    EXPECT_TRUE(wip == doneWip);
    EXPECT_TRUE(&one == doneWip->getResult());
    EXPECT_EQ(0, executor.numCompleted());

    delete doneWip;
}

// Tests that the Executor correctly executes two, self-terminating tasks.
TEST_F(ExecutorTest, TwoSelfTerminatingTasks) {

    Wip* wip1 = executor.submit(terminatingTask1);
    Wip* wip2 = executor.submit(terminatingTask2);

    Wip* doneWipA = executor.wait();
    Wip* doneWipB = executor.wait();

    EXPECT_TRUE(&one == doneWipA->getResult() ||
            &one == doneWipB->getResult());
    EXPECT_TRUE(&two == doneWipA->getResult() ||
            &two == doneWipB->getResult());
    EXPECT_TRUE(doneWipA->getResult() != doneWipB->getResult());
    EXPECT_EQ(0, executor.numCompleted());

    delete doneWipA;
    delete doneWipB;
}

// Tests that the Executor correctly stops an indefinite task.
TEST_F(ExecutorTest, IndefiniteTask) {

    Wip* wip1 = executor.submit(indefiniteTask1);

    EXPECT_EQ(0, executor.numCompleted());
    wip1->stop();
    Wip* doneWip = executor.wait();

    EXPECT_TRUE(wip1 == doneWip);
    EXPECT_EQ(true, doneWip->wasStopped());
    EXPECT_EQ(0, executor.numCompleted());

    delete doneWip;
}

// Tests that the Executor will accept the same indefinite task twice.
TEST_F(ExecutorTest, SameIndefiniteTask) {

    Wip* wips[2];

    wips[0] = executor.submit(indefiniteTask1);
    wips[1] = executor.submit(indefiniteTask1);

    sleep(1);
    EXPECT_EQ(0, executor.numCompleted());

    for (int i = 0; i < 2; i++) {
        wips[i]->stop();
        Wip* doneWip = executor.wait();
        EXPECT_TRUE(wips[i] == doneWip);
        EXPECT_TRUE(&one == doneWip->getResult());
        EXPECT_EQ(0, executor.numCompleted());
        delete doneWip;
    }
}

// Tests that the Executor can stop and clear all tasks.
TEST_F(ExecutorTest, StopAllAndClear) {

    Wip* wips[2];

    wips[0] = executor.submit(indefiniteTask1);
    wips[1] = executor.submit(indefiniteTask1);

    sleep(1);
    executor.stopAllAndClear();
    EXPECT_EQ(0, executor.numCompleted());
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

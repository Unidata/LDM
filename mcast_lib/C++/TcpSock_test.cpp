/**
 * This file tests class `TcpSock`.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See file "COPYING" in the top-level source-directory for usage
 * restrictions.
 *
 *       File: TcpSock_test.cpp
 * Created On: Jan 30, 2018
 *     Author: Steven R. Emmerson
 */
#include "TcpSock.h"

#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

namespace {

/// The fixture for testing class `TcpSock`
class TcpSockTest : public ::testing::Test
{
protected:
    // You can remove any or all of the following functions if its body
    // is empty.

    TcpSockTest()
    {
        // You can do set-up work for each test here.
    }

    virtual ~TcpSockTest()
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

// Tests construction of default server socket
TEST_F(TcpSockTest, ServerSocket)
{
    SrvrTcpSock srvrSock{};
    EXPECT_LT(0, srvrSock.getPort());
}

// Tests construction of bound server socket
TEST_F(TcpSockTest, BoundServerSocket)
{
    struct sockaddr_in localAddr = {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    SrvrTcpSock srvrSock{};
    EXPECT_LT(0, srvrSock.getPort());
    std::cout << "Server socket: " << srvrSock.to_string() << '\n';
}

// Tests connecting to server socket
TEST_F(TcpSockTest, ConnectingToServerSocket)
{
    struct sockaddr_in srvrAddr = {};
    srvrAddr.sin_family = AF_INET;
    srvrAddr.sin_port = 0;
    srvrAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    SrvrTcpSock srvrSock{};
    srvrAddr.sin_port = htons(srvrSock.getPort());

    srvrAddr.sin_port = htons(srvrSock.getPort());
    std::thread thread{[&srvrAddr] {
        TcpSock clntSock{};
        clntSock.connect(srvrAddr);
        std::cout << "Client socket: " << clntSock.to_string() << '\n';
        clntSock.send(&srvrAddr, sizeof(srvrAddr));
    }};
    thread.detach();

    auto connSock = srvrSock.accept();
    std::cout << "Connection socket: " << connSock.to_string() << '\n';
    struct sockaddr_in msg = {};
    EXPECT_EQ(sizeof(msg), connSock.recv(&msg, sizeof(msg)));
    EXPECT_EQ(0, std::memcmp(&srvrAddr, &msg, sizeof(msg)));
    EXPECT_EQ(0, connSock.recv(&msg, sizeof(msg)));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

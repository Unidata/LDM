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
};

// Tests construction of default server socket
TEST_F(TcpSockTest, ServerSocket)
{
    SrvrTcpSock srvrSock{InetFamily::IPV4};
    EXPECT_LT(0, srvrSock.getPort());
}

// Tests construction of bound server socket
TEST_F(TcpSockTest, BoundServerSocket)
{
    InetSockAddr srvrSockAddr{InetAddr{"127.0.0.1"}};
    SrvrTcpSock srvrSock{srvrSockAddr};
    EXPECT_LT(0, srvrSock.getPort());
    std::cout << "Server socket: " << srvrSock.to_string() << '\n';
}

// Tests writing and reading
TEST_F(TcpSockTest, WritingAndReading)
{
    SrvrTcpSock  srvrSock{InetAddr{"127.0.0.1"}};
    auto         srvrSockAddr = srvrSock.getLocalSockAddr();

    std::thread thread{[&srvrSockAddr] {
        TcpSock clntSock{srvrSockAddr.getFamily()};
        clntSock.connect(srvrSockAddr);
        std::cout << "Client socket: " << clntSock.to_string() << '\n';
        clntSock.write(&srvrSockAddr, sizeof(srvrSockAddr));
    }};
    thread.detach();

    auto connSock = srvrSock.accept();
    std::cout << "Connection socket: " << connSock.to_string() << '\n';
    decltype(srvrSockAddr) msg{InetFamily::IPV4};
    EXPECT_EQ(sizeof(msg), connSock.read(&msg, sizeof(msg)));
    EXPECT_EQ(0, std::memcmp(&srvrSockAddr, &msg, sizeof(msg)));
    EXPECT_EQ(0, connSock.read(&msg, sizeof(msg)));
}

// Tests vector writing and reading
TEST_F(TcpSockTest, VectorWritingAndReading)
{
    SrvrTcpSock  srvrSock{InetAddr{"127.0.0.1"}};
    auto         srvrSockAddr = srvrSock.getLocalSockAddr();

    std::thread thread{[&srvrSockAddr] {
        TcpSock clntSock{srvrSockAddr.getFamily()};
        clntSock.connect(srvrSockAddr);
        struct iovec iov[2];
        iov[0].iov_base = &srvrSockAddr;
        iov[0].iov_len = sizeof(srvrSockAddr);
        iov[1] = iov[0];
        clntSock.writev(iov, 2);
    }};
    thread.detach();

    auto connSock = srvrSock.accept();
    std::cout << "Connection socket: " << connSock.to_string() << '\n';
    decltype(srvrSockAddr) msg{InetFamily::IPV4};
    struct iovec iov[2];
    iov[0].iov_base = &msg;
    iov[0].iov_len = sizeof(msg);
    iov[1] = iov[0];
    EXPECT_EQ(2*sizeof(msg), connSock.readv(iov, 2));
    EXPECT_EQ(0, std::memcmp(&srvrSockAddr, &msg, sizeof(msg)));
    EXPECT_EQ(0, connSock.read(&msg, sizeof(msg)));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

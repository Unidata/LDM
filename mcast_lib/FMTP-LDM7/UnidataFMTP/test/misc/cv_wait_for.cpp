/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      cv_wait_for.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Feb 24, 2015
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     conditional variable: wait_for usage demo.
 *
 * This is a simple demo to show how to use conditional variable and its
 * wait_for method. In this test, two threads are used, one for timer and
 * another for caller. Timer would either sleep for enough time or being
 * interrupted by the caller using a conditional variable.
 */


#include <iostream>
#include <condition_variable>
#include <chrono>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

std::condition_variable timerwaking;
std::mutex mtx;

static void* timerThread(void* ptr)
{
    std::unique_lock<std::mutex> lock(mtx);
    unsigned long time = 5 * 1000000000lu;
    timerwaking.wait_for(lock, std::chrono::nanoseconds(time));
    std::cout << "timer wakes up" << std::endl;

    return NULL;
}

int main(int argc, char const* argv[])
{
    pthread_t t;
    pthread_create(&t, NULL, timerThread, NULL);
    pthread_detach(t);
    sleep(1);
    timerwaking.notify_all();
    while(1);
    return 0;
}

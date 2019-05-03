/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      elapsedtime.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Mar 11, 2015
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
 * @brief     chrono: high resolution clock usage demo.
 */


#include <chrono>
#include <ctime>
#include <iostream>
#include <ratio>

typedef std::chrono::high_resolution_clock myClock;

int main ()
{
    auto t1 = myClock::now();

    std::cout << "printing out 1000 stars...\n";
    for (int i=0; i<1000; ++i) std::cout << "*";
    std::cout << std::endl;

    auto t2 = myClock::now();

    std::chrono::duration<double> time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

    std::cout << "Since Epoch: " << t1.time_since_epoch().count() << std::endl;
    std::cout << "Since Epoch: " << t2.time_since_epoch().count() << std::endl;

    std::cout << "It took " << time_span.count() << " seconds." << std::endl;

    std::cout << "It took " <<
        std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()
        << " nanoseconds." << std::endl;
    std::cout << std::endl;

    return 0;
}

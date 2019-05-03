/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      cv_wait_for.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      July 5, 2015
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
 * @brief     Test program to get hostname.
 */


#include <unistd.h>
#include <iostream>
#include <string>

int main(int argc, char const* argv[])
{
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string mystr(hostname);

    std::cout << mystr << std::endl;

    return 0;
}

/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      set_eraserange.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      August 16, 2015
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
 * @brief     A simple test program for std::set erasing a range.
 */


#include <iostream>
#include <set>
#include <stdint.h>

int main(int argc, char const* argv[])
{
    int prodnum = 500;
    int initval[prodnum];
    for (int i = 0; i < prodnum; ++i) {
        initval[i] = i;
    }
    std::set<uint32_t>* prodset = new std::set<uint32_t>(initval, initval + prodnum);
    std::set<uint32_t>::iterator it;

    std::cout << "Set begin: " << *(prodset->begin()) << std::endl;
    std::cout << "Set end: " << *(prodset->rbegin()) << std::endl;

    it = prodset->find(50);
    prodset->erase(prodset->begin(), it);

    std::cout << "Set begin: " << *(prodset->begin()) << std::endl;
    std::cout << "Set end: " << *(prodset->rbegin()) << std::endl;

    return 0;
}

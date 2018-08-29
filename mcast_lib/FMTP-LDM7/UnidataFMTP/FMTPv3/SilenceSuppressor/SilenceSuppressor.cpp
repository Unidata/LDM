/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      SilenceSuppressor.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      August 1, 2015
 *
 * @section   LICENSE
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or（at your option）
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details at http://www.gnu.org/copyleft/gpl.html
 *
 * @brief     Silence suppressor implementation. Suppresses silence when
 *            replaying metadata.
 */


#include "SilenceSuppressor.h"


/**
 * Constructor of SilenceSuppressor.
 */
SilenceSuppressor::SilenceSuppressor(int prodnum)
{
    int initval[prodnum];
    for (int i = 0; i < prodnum; ++i) {
        initval[i] = i;
    }
    std::unique_lock<std::mutex> lock(mtx);
    prodset = new std::set<uint32_t>(initval, initval + prodnum);
}


/**
 * Destructor of SilenceSuppressor.
 */
SilenceSuppressor::~SilenceSuppressor()
{
    std::unique_lock<std::mutex> lock(mtx);
    prodset->clear();
    delete prodset;
}


/**
 * Clears a range of products in the prodset.
 *
 * @param[in] end    Prodnum of the end of the range
 */
void SilenceSuppressor::clearrange(uint32_t end)
{
    std::unique_lock<std::mutex> lock(mtx);
    std::set<uint32_t>::iterator it = prodset->find(end);
    prodset->erase(prodset->begin(), it);
}


/**
 * Queries the first prodindex in the prodset.
 *
 * @return    The prodindex of the first product.
 */
uint32_t SilenceSuppressor::query()
{
    std::unique_lock<std::mutex> lock(mtx);
    return *(prodset->begin());
}


/**
 * Removes the given product from the prodset.
 *
 * @param[in] prodindex    Product index to be removed.
 * @return    The status of std::set::erase() operation.
 */
bool SilenceSuppressor::remove(uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mtx);
    if (prodset->erase(prodindex)) {
        return true;
    }
    else {
        return false;
    }
}

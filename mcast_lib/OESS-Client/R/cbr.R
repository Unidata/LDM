##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      cbr.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Nov. 4, 2015
#
# @section   LICENSE
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or（at your option）
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details at http://www.gnu.org/copyleft/gpl.html
#
# @brief   Computes the cumulative bytes ratio (CBR) for each file.


rawcsv <- read.csv('~/highRTT.csv', sep = ',', comment.char = '#')
cbr <- rawcsv[1:5000,]
plot(cbr[[2]], pch=16, type="l", xlab = 'product index',
     ylab = 'cumulative bytes ratio (%)')
title('Cumulative bytes ratio - 55 ms RTT')

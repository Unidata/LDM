##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      latency-plot.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Mar. 3, 2016
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
# @brief     Plot the latency histogram.


par(mar=c(6.1,6.5,4.1,2.1))
# cuts off the long tail
tmp <- rtt_1ms[rtt_1ms<0.2]
hist(tmp, breaks = 60, main='', cex.lab=1.5, cex.axis=1.5, xlab='Latency (seconds)')

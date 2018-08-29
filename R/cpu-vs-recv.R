##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      cpu-vs-recv.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Jan 24, 2016
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
# @brief     Plot the CPU usage against number of receivers graph.


par(mar=c(6.1,6.5,4.1,2.1))
plot(c(4,8,12,16,20,24), ldm6_1, type='o', col='blue3', pch=22, lwd=3,
     cex.lab=1.5, cex.axis=1.5, xlab='Number of Receivers',
     ylab='CPU Utilization (%)', ylim=c(0,6), xaxp=c(4,24,5))
lines(c(4,8,12,16,20,24), ldm6_0, type='o', col='brown1', pch=25, lty=2, lwd=3)
lines(c(4,8,12,16,20,24), ldm7_0, type='o', col='forestgreen', pch=8, lty=5, lwd=3)
lines(c(4,8,12,16,20,24), ldm7_1, type='o', col='gold1', pch=1, lty=6, lwd=3)
grid()

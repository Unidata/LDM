##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      bw-vs-min-traffic-pattern.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Jan 23, 2016
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
# @brief     Plot the bandwidth usage against minutes graph.


# generate plot as 9 : 13.5
par(mfrow=c(2,1), mar=c(5,5,4,1))
plot(1:59, bw6, col='red', type='l', lwd=3, lty=5, xlab='Minutes',
     ylab='BW (Mbps)', main='Sender NIC Bandwidth (BW) Usage', cex.lab=1.5,
     cex.axis=1.5, cex.main=1.5)
lines(1:59, bw7, col='blue', type='l', lwd=3)
plot(1:60, data, type='h', lwd=3, xlab='Minutes', ylab='Agg. Size (MB)',
     main='File-Stream Traffic Pattern', cex.lab=1.5, cex.axis=1.5, cex.main=1.5)

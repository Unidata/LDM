##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      thru-vs-rtt.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Mar. 1, 2016
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
# @brief     Plot the throughput vs. RTT graph


par(mar=c(6.1,6.5,4.1,2.1))
plot(c(1, 10, 20, 50, 100), rtt500, type='o', col='red', lwd=3, xlab='RTT (ms)',
     ylab='Aggregate Throughput (Mbps)', cex.lab=1.5, cex.axis=1.5, pch=21)
lines(c(1, 10, 20, 50, 100), rtt20, type='o', col='blue', lwd=3, pch=24)
legend('topright', col=c('red', 'blue'), c('500 Mbps', '20 Mbps'),
       lty=1, pch=c(21, 24), lwd=3)
grid()

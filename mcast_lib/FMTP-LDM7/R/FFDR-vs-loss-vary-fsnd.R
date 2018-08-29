##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      FFDR-vs-loss-vary-fsnd.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Jan 22, 2016
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
# @brief     Plot the FFDR against loss rate graph.


par(mar=c(6.1,6.5,4.1,2.1))
plot(c(0,0.5,1.0,1.5,2.0), m16_5k, type='o', col='blue3', pch=22,
     xlab='Packet Loss Rate (%)', ylab='FFDR (%)', cex.lab=1.5, cex.axis=1.5, lwd=3)
lines(c(0,0.5,1.0,1.5,2.0), m8_5k, type='o', col='brown1', pch=25, lty=2, lwd=3)
lines(c(0,1.0,2.0), m8_1m, type='o', col='gold1', pch=21, lty=4, lwd=3)
lines(c(0,1.0,2.0), m16_1m, type='o', col='purple', pch=23, lty=6, lwd=3)
grid()

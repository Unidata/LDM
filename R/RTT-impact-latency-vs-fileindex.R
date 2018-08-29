##
# Copyright (C) 2016 University of Virginia. All rights reserved.
#
# @file      RTT-impact-latency-vs-fileindex.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Apr. 12, 2016
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
# @brief     Plot the 4 ideal/real latency plots.


par(mar=c(6.1,6.5,4.1,2.1))
plot(real_8G[1:200], type='l', col='red', xlim=c(0,250), ylim=c(0,6) ,lwd=3,
     xlab = 'File Index', ylab = 'Latency (ms)', cex.lab=1.5, cex.axis=1.5)
lines(real_500M[1:200], type='l', col='blue', ylim=c(0,6), lwd=3)
lines(ideal_500M[1:200], type='l', col='green', ylim=c(0,6), lwd=3)
lines(ideal_8G[1:200], type='l', col='purple', ylim=c(0,6), lwd=3)
grid()

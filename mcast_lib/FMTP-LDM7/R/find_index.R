##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      find_index.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      July 4, 2015
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
# @brief     Calculates the cumulative arrival time of a given period and finds
#            its index.


# reads raw csv file from disk
ngrid <- read.csv('~/Workspace/IDD_replay/day1NGRID.csv')
# extracts the second column, which is the arrival time
arrival <- ngrid[2]
# sets cumulative arrival time (one hour)
cumulative <- 24000000
# finds the first arrival time that is longer than one hour
one_hr <- min(arrival[apply(arrival, 1, function(row) {all(row > cumulative)}),])
# finds the index of the first match
one_hr_index <- which(one_hr == arrival)
# prints on screen
cat('first match:', one_hr)
cat('first match index:', one_hr_index)

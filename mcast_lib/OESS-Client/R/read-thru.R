##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      read-thru.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Jan 30, 2016
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
# @brief     Reads throughput values from csv files.


csvpath <- '~/Workspace/LDM6-LDM7-LOG/thru-vs-rate/Lossy-16nodes/LDM7/16nodes-60Mbps-fsnd1000000/csv/'
csvfiles <- list.files(path = csvpath, pattern = '.csv')
thru_arr <- array()
for (i in 1:length(csvfiles))
{
  assign(csvfiles[i], read.csv(paste(csvpath, csvfiles[i], sep=''))[[5]])
  thru_arr <- cbind(thru_arr, get(csvfiles[i]))
}
thru_arr <- thru_arr[,-1]
avg_thru <- matrix(0, nrow = nrow(thru_arr))
for (i in 1:nrow(thru_arr))
{
  avg_thru[i] <- mean(thru_arr[i,])
}
thru <- mean(avg_thru)/1000000

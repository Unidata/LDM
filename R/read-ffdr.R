##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      read-ffdr.R
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
# @brief     Reads FFDR values from csv files.


csvpath <- '~/Workspace/LDM6-LDM7-LOG/new-thru-vs-rate/LDM7-0.01lossy/16nodes/fsnd5000-30Mbps/csv/'
csvfiles <- list.files(path = csvpath, pattern = '.csv')
ffdr_arr <- array()
for (i in 1:length(csvfiles))
{
  assign(csvfiles[i], read.csv(paste(csvpath, csvfiles[i], sep=''))[[6]])
  ffdr_arr <- cbind(ffdr_arr, get(csvfiles[i]))
}
ffdr_arr <- ffdr_arr[,-1]
avg_ffdr <- matrix(0, nrow = nrow(ffdr_arr))
for (i in 1:nrow(ffdr_arr))
{
  avg_ffdr[i] <- mean(ffdr_arr[i,])
}
ffdr <- mean(avg_ffdr)

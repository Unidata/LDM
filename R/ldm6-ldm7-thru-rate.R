##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      ldm6-ldm7-thru-rate.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Jan 28, 2016
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
# @brief     Plot the throughput against rate graph.


csvpath <- '~/Workspace/LDM6-LDM7-LOG/WAN-8nodes-LDM6/WAN-8nodes-10Mbps/csv/'
ldm6 <- matrix(0, nrow = 6)
ldm7 <- matrix(0, nrow = 7)
for (n in 1:6)
{
  csvfiles <- list.files(path = csvpath, pattern = '.csv')
  thru_arr <- array()
  for (i in 1:length(csvfiles))
  {
    assign(csvfiles[i], read.csv(paste(csvpath, csvfiles[i], sep=''))[['Throughput..bps.']])
    thru_arr <- cbind(thru_arr, get(csvfiles[i]))
  }
  thru_arr <- thru_arr[,-1]
  avg_thru <- matrix(0, nrow = nrow(thru_arr))
  for (i in 1:nrow(thru_arr))
  {
    avg_thru[i] <- mean(thru_arr[i,])
  }
  ldm6[n] <- mean(avg_thru)
  csvpath <- gsub(toString(n*10), toString((n+1)*10), csvpath)
}

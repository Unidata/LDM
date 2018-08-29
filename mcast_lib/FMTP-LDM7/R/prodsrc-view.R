##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      prodsrc-view.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Oct. 17, 2015
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
# @brief   Computes the VCMTP-sourced rate for each aggregate.


rates <- numeric()
agg <- read.csv('~/agg.csv', sep = ',', comment.char = '#')
start <- agg[[1]]
end <- agg[[2]]
data <- read.csv('~/prodsrc-recv2-24hr.log', sep = ':', comment.char = '#')
y <- ifelse(data[[1]] == 'VCMTP', 1, 0)
x <- data[[2]]
for (i in 1:nrow(agg))
{
  start_id <- 0
  end_id <- 0
  row_id <- match(start[i], x)
  if (is.na(row_id)) {
    start_id <- which.min(abs(x - start[i]))
  }
  else {
    start_id <- row_id
  }
  row_id <- match(end[i], x)
  if (is.na(row_id)) {
    end_id <- which.min(abs(x - end[i]))
  }
  else {
    end_id <- row_id
  }
  rates[i] <- sum(y[start_id:end_id]) / length(y[start_id:end_id]) * 100
}
plot(rates, pch=16, type="h", xlab = 'Aggregate group',
     ylab = 'VCMTP-sourced rate (%)')
title('Grouped VCMTP-sourced rates - recv2 (24-hr NGRID)')

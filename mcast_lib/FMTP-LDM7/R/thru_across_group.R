##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      thru_across_group.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      August 6, 2015
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
# @brief     Plot the throughput across 10 runs

prefix <- "~/Workspace/VCMTP_LOG/august01-2015-UFL/csv-500MB/"
filename <- "Expt10-UFL-run"
varprefix <- "Expt10_UFL_run"
group <- numeric()
for(i in 1:10)
{
  fullname <- paste(prefix, filename, sep="")
  varname <- paste(varprefix, i, sep="")
  oname <- paste(fullname, i, sep="")
  assign(varname, read.csv(paste(oname, ".csv", sep="")))
  # extracts lossless throughput only
  assign(varname, get(varname)[[5]])
  # turn bps intp Mbps
  assign(varname, get(varname) / 1000000)
  # the number in square brackets is the group index
  group <- c(group, get(varname)[37])
}

# For plot
boxlist <- list('Group1'=g1, 'Group2'=g2, 'Group3'=g3, 'Group4'=g4, 'Group5'=g5)
plotcol <- c('indianred', 'forestgreen', 'cadetblue', 'goldenrod', 'burlywood')
options(scipen=10)
boxplot(boxlist, col=plotcol, las=1, outline=FALSE)
title('Throughput vs. Aggregate Size', xlab='Aggregate group number',
      ylab='Lossless Throughput per Aggregate (Mbps)')
mtext('5 aggregate groups from Expt10-UFL-AggSize')
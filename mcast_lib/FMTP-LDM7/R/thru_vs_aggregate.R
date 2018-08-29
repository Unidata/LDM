##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      thru_vs_aggregate.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      July 20, 2015
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
# @brief     Plot the throughput against aggregate size graph.


prefix <- "~/Workspace/VCMTP_LOGS/ExoGENI/july8-2015-UMass-UCD-PSC/PSC/
           allcsv/csv-1MB/"
filename <- "Expt1-PSC-run"
varprefix <- "Expt1_PSC_run"
thru_1MB <- numeric()
for(i in 1:10)
{
  fullname <- paste(prefix, filename, sep="")
  varname <- paste(varprefix, i, sep="")
  oname <- paste(fullname, i, sep="")
  assign(varname, read.csv(paste(oname, ".csv", sep="")))
  assign(varname, get(varname)[[5]])
  # merge throughput into a vector
  thru_1MB <- c(thru_1MB, get(varname))
}
# convert throughput from bps to Mbps
thru_1MB <- thru_1MB / 1000000


thru_1MB <- read.csv('~/recv/csv-1MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_10MB <- read.csv('~/recv/csv-10MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_20MB <- read.csv('~/recv/csv-20MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_50MB <- read.csv('~/recv/csv-50MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_100MB <- read.csv('~/recv/csv-100MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_200MB <- read.csv('~/recv/csv-200MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_500MB <- read.csv('~/recv/csv-500MB/Expt10-UFL-run4.csv')[[5]]/1000000
thru_1000MB <- read.csv('~/recv/csv-1000MB/Expt10-UFL-run4.csv')[[5]]/1000000
# For plot
par(mar=c(5.1,5.1,4.1,2.1))
boxlist <- list('1'=thru_1MB, '10'=thru_10MB, '20'=thru_20MB, '50'=thru_50MB,
                '100'=thru_100MB, '200'=thru_200MB, '500'=thru_500MB, '1000'=thru_1000MB)
plotcol <- c('indianred1', 'green4', 'lightskyblue', 'khaki', 'coral', 'purple',
             'dodgerblue', 'darkorange')
options(scipen=10)
boxplot(boxlist, col=plotcol, las=1, outline=FALSE, xaxt='n', yaxt='n')
axis(side=1, at=1, labels='1', cex.axis=1.5)
axis(side=1, at=2, labels='10', cex.axis=1.5)
axis(side=1, at=3, labels='20', cex.axis=1.5)
axis(side=1, at=4, labels='50', cex.axis=1.5)
axis(side=1, at=5, labels='100', cex.axis=1.5)
axis(side=1, at=6, labels='200', cex.axis=1.5)
axis(side=1, at=7, labels='500', cex.axis=1.5)
axis(side=1, at=8, labels='1000', cex.axis=1.5)
axis(side=2, at=seq(92,104,4), cex.axis=1.5)
title(xlab='Aggregate File-set (group) Size (MB)', ylab='Throughput (Mbps)', cex.lab=1.5)

boxlist <- list('Group1'=g1, 'Group2'=g2, 'Group3'=g3, 'Group4'=g4, 'Group5'=g5)
boxplot(boxlist, las=1, outline=FALSE, xaxt='n', yaxt='n')
axis(side=1, at=1, labels='Group1', cex.axis=1.5)
axis(side=1, at=2, labels='Group2', cex.axis=1.5)
axis(side=1, at=3, labels='Group3', cex.axis=1.5)
axis(side=1, at=4, labels='Group4', cex.axis=1.5)
axis(side=1, at=5, labels='Group5', cex.axis=1.5)
axis(side=2, at=seq(95,99,0.2), cex.axis=1.5)
title(xlab='Aggregate Group Index', ylab='Throughput per 200 MB Aggregate (Mbps)', cex.lab=1.5)
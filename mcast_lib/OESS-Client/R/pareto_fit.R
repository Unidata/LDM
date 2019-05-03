##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      pareto_fit.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      Apr. 16, 2015
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
# @brief     reads source data and uses pareto model to fit it.


# before running all the commands related to pareto and fitdistr, make sure
# that their requirements have been installed.
# pareto functions are relying on actuar
require(actuar)

# fitdist function is relying on fitdistrplus package
require(fitdistrplus)

# read source data from a csv file (path might need to be changed)
# the csv file only has one column which is the file size in bytes
# the data source is from LDM site @UCAR, feedtype is NGRID.
sizes <- read.csv('~/Workspace/VCMTP_LOGS/size.csv')

# transposition of a data frame into single-row matrix
sizes <- t(sizes)

# cast matrix into vector
sizevec <- as.vector(sizes)

# use fitdistrplus package to fit pareto distribution
# by default, fitdist uses the MLE method in fitting
# fitdist returns an object of class "fitdist", it's a list with the several
# components. What we're interested here are the estimates and their sd.
fp <- fitdist(sizevec, 'pareto', start=c(shape=3, scale=min(sizevec)), discrete=TRUE)

# extract the estimate field of the returned list. The estimate field is
# actually a named number. One is named shape, which is the alpha in Pareto
# distribution. And the other is named scale. Here we extracts the estimated
# shape value.
shape <- fp$estimate[["shape"]]

# Extract the estimated scale value
scale <- fp$estimate[["scale"]]

# use the estimated parameters to generate a pareto simulation as large as
# the raw dataset size
simu <- rpareto(length(sizevec), scale=scale, shape=shape)

# use simu as the reference distribution and test the similarity by KS test
# KS test will return a list of 5 elements, one of them is the p-value
kslist <- ks.test(sizevec, simu)

# get p-value of the KS test
# this p-value is the final evaluation of the fitness of the model
pval <- kslist[2][["p.value"]]

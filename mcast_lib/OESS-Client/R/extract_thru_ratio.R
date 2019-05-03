##
# Copyright (C) 2015 University of Virginia. All rights reserved.
#
# @file      extract_thru_ratio.R
# @author    Shawn Chen <sc7cq@virginia.edu>
# @version   1.0
# @date      August 16, 2015
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
# @brief   Extracts the successful throughput and block retransmission ratio.


path <- "~/Workspace/VCMTP_LOG/"
expt <- "18"
node <- "2"
host <- "TAMU"
prefix <- paste(path, "Expt-", expt, "/recv-node", node, "/", sep="")
filename <- paste("Expt", expt, "-", host, "-run", sep="")
varprefix <- paste("Expt", expt, "_recvnode", node, "_run", sep="")
ratiomatrix <- paste("Expt", expt, "_recvnode", node, "_ratiomatrix", sep="")
thrumatrix <- paste("Expt", expt, "_recvnode", node, "_thrumatrix", sep="")
fdrmatrix <- paste("Expt", expt, "_recvnode", node, "_fdrmatrix", sep="")
thru_mat <- numeric()
ratio_mat <- numeric()
fdr_mat <- numeric()
for(i in 1:10)
{
  csvname <- paste(prefix, filename, i, sep="")
  varname <- paste(varprefix, i, sep="")
  thru_s <- paste(varname, "_thru_s", sep="")
  block_retx_ratio <- paste(varname, "_ratio", sep="")
  assign(varname, read.csv(paste(csvname, ".csv", sep="")))
  # convert from bps to Mbps
  assign(thru_s, get(varname)[[6]] / 1000000)
  assign(block_retx_ratio, get(varname)[[13]])
  thru_mat <- rbind(thru_mat, get(varname)[[6]] / 1000000)
  ratio_mat <- rbind(ratio_mat, get(varname)[[13]])
  fdr_mat <- rbind(fdr_mat, get(varname)[[9]])
}
assign(ratiomatrix, ratio_mat)
assign(thrumatrix, thru_mat)
assign(fdrmatrix, fdr_mat)


# for Expt 11-12-13
group <- numeric()
mean <- numeric()
sd <- numeric()
f_rcv <- numeric()
if (as.numeric(expt) == 11)
{
  f <- "10"
} else if (as.numeric(expt) == 12) {
  f <- "5"
} else {
  f <- "2"
}
for(i in 1:length(fdr_mat[1,]))
{
  group <- c(group, i)
  mean <- c(mean, mean(fdr_mat[,i]))
  sd <- c(sd, sd(fdr_mat[,i]))
  f_rcv <- c(f_rcv, f)
}
if (as.numeric(expt) == 11)
{
  df_fdr <- data.frame(group, mean, sd, f_rcv)
} else {
  df_fdr <- rbind(df_fdr, data.frame(group, mean, sd, f_rcv))
}


# for expt18
group <- numeric()
mean <- numeric()
sd <- numeric()
RTT <- numeric()
if (as.numeric(node) == 1)
{
  rtt <- "1 ms"
} else {
  rtt <- "100 ms"
}
for(i in 1:length(fdr_mat[1,]))
{
  group <- c(group, i)
  mean <- c(mean, mean(fdr_mat[,i]))
  sd <- c(sd, sd(fdr_mat[,i]))
  RTT <- c(RTT, rtt)
}
if (as.numeric(node) == 1)
{
  df_fdr <- data.frame(group, mean, sd, RTT)
} else {
  df_fdr <- rbind(df_fdr, data.frame(group, mean, sd, RTT))
}


# for expt19
group <- numeric()
mean <- numeric()
sd <- numeric()
f_rcv <- numeric()
if (as.numeric(node) == 1)
{
  f <- "10"
} else {
  f <- "5"
}
for(i in 1:length(thru_mat[1,]))
{
  group <- c(group, i)
  mean <- c(mean, mean(thru_mat[,i]))
  sd <- c(sd, sd(thru_mat[,i]))
  f_rcv <- c(f_rcv, f)
}
if (as.numeric(node) == 1)
{
  df_thru <- data.frame(group, mean, sd, f_rcv)
} else {
  df_thru <- rbind(df_thru, data.frame(group, mean, sd, f_rcv))
}

group <- numeric()
mean <- numeric()
sd <- numeric()
f_rcv <- numeric()
if (as.numeric(node) == 1)
{
  f <- "10"
} else {
  f <- "5"
}
for(i in 1:length(ratio_mat[1,]))
{
  group <- c(group, i)
  mean <- c(mean, mean(ratio_mat[,i]))
  sd <- c(sd, sd(ratio_mat[,i]))
  f_rcv <- c(f_rcv, f)
}
if (as.numeric(node) == 1)
{
  df_ratio <- data.frame(group, mean, sd, f_rcv)
} else {
  df_ratio <- rbind(df_ratio, data.frame(group, mean, sd, f_rcv))
}

group <- numeric()
mean <- numeric()
sd <- numeric()
f_rcv <- numeric()
if (as.numeric(node) == 1)
{
  f <- "10"
} else {
  f <- "5"
}
for(i in 1:length(fdr_mat[1,]))
{
  group <- c(group, i)
  mean <- c(mean, mean(fdr_mat[,i]))
  sd <- c(sd, sd(fdr_mat[,i]))
  f_rcv <- c(f_rcv, f)
}
if (as.numeric(node) == 1)
{
  df_fdr <- data.frame(group, mean, sd, f_rcv)
} else {
  df_fdr <- rbind(df_fdr, data.frame(group, mean, sd, f_rcv))
}


# For plot
options(scipen=10)
boxplot(t(Expt19_recvnode1_thrumatrix), las = 1, outline = FALSE)
boxplot(t(Expt19_recvnode2_thrumatrix), las = 1, outline = FALSE)
boxplot(t(Expt18_recvnode1_ratiomatrix), las = 1, outline = FALSE)
boxplot(t(Expt18_recvnode2_ratiomatrix), las = 1, outline = FALSE)

# throughput
par(mfrow = c(2,1))
par(mar = c(3.5, 4, 3, 1))
boxplot(Expt19_recvnode1_thrumatrix, las = 1, outline = FALSE, cex.axis = 0.8)
title(main = expression(paste("Throughput across 400 minutes, ", f[rcv],
                              " = 10    (recv1)")), cex.main = 1.1)
mtext("Group number", side = 1, line = 2.3)
mtext("Successful throughput (Mbps)", side = 2, line = 2.7)

boxplot(Expt19_recvnode2_thrumatrix, las = 1, outline = FALSE, cex.axis = 0.8)
title(main = expression(paste("Throughput across 400 minutes, ", f[rcv],
                              " = 5    (recv2)")), cex.main = 1.1)
mtext("Group number", side = 1, line = 2.3)
mtext("Successful throughput (Mbps)", side = 2, line = 2.7)


# block retx ratio
par(mfrow = c(2,1))
par(mar = c(3.5, 4.5, 3, 1))
boxplot(Expt19_recvnode1_ratiomatrix, las = 1, outline = FALSE, cex.axis = 0.8)
title(main = expression(paste("Block retx ratio across 400 minutes, ", f[rcv],
                              " = 10     (recv1)")), cex.main = 1.1)
mtext("Group number", side = 1, line = 2.3)
mtext("Block retx ratio (%)", side = 2, line = 3.2)

boxplot(Expt19_recvnode2_ratiomatrix, las = 1, outline = FALSE, cex.axis = 0.8)
title(main = expression(paste("Block retx ratio across 400 minutes, ", f[rcv],
                              " = 5    (recv2)")), cex.main = 1.1)
mtext("Group number", side = 1, line = 2.3)
mtext("Block retx ratio (%)", side = 2, line = 3.2)


# for expt18
pd <- position_dodge(0.1)
ggplot(df_fdr, aes(x=group, y=mean, colour=RTT, group=RTT)) + 
geom_errorbar(aes(ymin=mean-sd, ymax=mean+sd), colour="darkgrey", width=1.0, position=pd) +
geom_line(position=pd) + geom_point(position=pd, size=3, shape=21, fill="white") +
xlab("Group number") + ylab("Successful FDR (%)") +
ggtitle("Successful FDR across 400 minutes") + theme_bw()

# ggplot
pd <- position_dodge(0.1)
ggplot(df_thru, aes(x=group, y=mean, colour=f_rcv, group=f_rcv)) + 
geom_errorbar(aes(ymin=mean-sd, ymax=mean+sd), colour="darkgrey", width=1.0, position=pd) +
geom_line(position=pd) + geom_point(position=pd, size=3, shape=21, fill="white") +
xlab("Group number") + ylab("Successful throughput (Mbps)") +
ggtitle("Throughput across 400 minutes") + theme_bw()

pd <- position_dodge(0.1)
ggplot(df_ratio, aes(x=group, y=mean, colour=f_rcv, group=f_rcv)) + 
geom_errorbar(aes(ymin=mean-sd, ymax=mean+sd), colour="darkgrey", width=1.0, position=pd) +
geom_line(position=pd) + geom_point(position=pd, size=3, shape=21, fill="white") +
xlab("Group number") + ylab("Block retx ratio (%)") +
ggtitle("Block retx ratio across 400 minutes") + theme_bw()

pd <- position_dodge(0.1)
ggplot(df_fdr, aes(x=group, y=mean, colour=f_rcv, group=f_rcv)) + 
geom_errorbar(aes(ymin=mean-sd, ymax=mean+sd), colour="darkgrey", width=1.0, position=pd) +
geom_line(position=pd) + geom_point(position=pd, size=3, shape=21, fill="white") +
xlab("Group number") + ylab("Successful FDR (%)") +
ggtitle("Successful FDR across 400 minutes") + theme_bw()

plot(retx_d, type="o",col="red", xlab='Aggregate number', ylab='Retx ratio (%)')
lines(retx_e, type='o', col='blue')
lines(retx_f, type='o', col='green')
title('f_rcv study (Expt D/E/F)')

ggplot(df, aes(x=id, y=val, colour=group, group=group)) + 
geom_line() + geom_point() + xlab("Group number") + ylab("Block Retx Ratio (%)") +
ggtitle("Block Retx Ratio Study (Expt F)") + theme_bw()


pd <- position_dodge(0.1)
ggplot(data = df.melted, aes(x = x, y = value, color = variable)) + geom_point() +
  geom_line(position=pd) + geom_point(aes(shape=variable, color=variable), size=3) +
  xlab("Group number") + ylab("Block Retx Ratio (%)") +
  ggtitle("BRR study w/ and w/o background traffic (frcv=10)") + theme_bw() +
  theme(text = element_text(size=20), axis.text = element_text(size=20))
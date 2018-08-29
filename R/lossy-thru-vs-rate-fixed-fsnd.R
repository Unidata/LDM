par(mar=c(6.1,6.5,4.1,2.1))
plot(c(10,20,30,40,50,60), ldm7_8, type='o', col='blue3', pch=22,
     xlab='base rate, r (Mbps)', ylab='Average throughput (Mbps)',
     cex.lab=1.5, cex.axis=1.5, lwd=3)
lines(c(10,20,30,40,50,60), ldm7_16, type='o', col='brown1', pch=25, lty=2, lwd=3)
lines(c(10,20,30,40,50,60), ldm6_8, type='o', col='forestgreen', pch=8, lty=5, lwd=3)
lines(c(10,20,30,40,50,60), ldm6_16, type='o', col='gold1', pch=1, lty=6, lwd=3)
grid()
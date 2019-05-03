#!/bin/bash

LOG=$(printenv LDMHOME)/tc_mon.log
while true
do
    date -u >> $LOG
    uptime >> $LOG
    tc -s qdisc >> $LOG
    echo -en "\n\n" >> $LOG
    sleep 60
done

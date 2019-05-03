#!/bin/bash

LOG=$(printenv LDMHOME)/cpu_measure.log
while true
do
    date -u >> $LOG
    uptime >> $LOG
    ps -eo pcpu,pid,args | grep -E 'ldmd|mldm' >> $LOG
    echo -en "\n\n" >> $LOG
    sleep 60
done

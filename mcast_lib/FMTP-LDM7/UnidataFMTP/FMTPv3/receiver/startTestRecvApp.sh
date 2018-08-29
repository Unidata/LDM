#!/bin/bash

# get the second IP address in OS
bindip=`hostname -I | awk -F ' ' '{print $2}'`

TCPADDR="10.10.1.1"
TCPPORT="1234"
MCASTADDR="224.0.0.1"
MCASTPORT="5173"

nohup ./testRecvApp $TCPADDR $TCPPORT $MCASTADDR $MCASTPORT $bindip &> /dev/null &
#nohup ./testRecvApp $TCPADDR $TCPPORT $MCASTADDR $MCASTPORT $bindip &> /root/stdout &

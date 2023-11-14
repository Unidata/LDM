#!/bin/bash
#
# File:         noaaportBlender.sh
# Author: cooper@ucar.edu
# See file ../COPYRIGHT for copying and redistribution conditions.
#
# Description: This is a shell script to monitor and keep
#  blender() and noaaportIngester() pair running
# Original code 2023-03-02, cooper@ucar.edu
# Updated with comments 2023-11-13, cooper@ucar.edu

# Set system-wide profile for script
if [ -s /etc/profile ]; then
  source /etc/profile
fi

# Test for existence of noaaportIngester, then set full path
Test=`which noaaportIngester`
if [ -z "${Test}" ]; then
  echo "noaaportIngester not in path."
  exit
else
  NOAAPORTINGESTER=`realpath ${Test}`
fi

# Test for existence of blender, then set full path
Test=`which blender`
if [ -z "${Test}" ]; then
  echo "blender not in path."
  exit
else
  BLENDER=`realpath ${Test}`
fi

# Test for existence of ulogger, then set full path
Test=`which ulogger`
if [ -z "${Test}" ]; then
  echo "ulogger not in path."
  exit
else
  ULOGGER=`realpath ${Test}`
fi

# Initialize process id variables, set default script log,
#   and default loop delay
npProcess=""
blProcess=""
terminate="0"
thisScriptLog="/tmp/noaaportBlender.log"
scriptDelay="2"

# Internal function definitions BEGIN
#
function printUsage
{
  printf "usage: ${0} [-v][-x][-b <blender log>][-l <noaaportIngester log>][-f <fifo>][-p <port>][-t <timeOut>] -F <fanout server IP>[:<port>]\n"
  printf "  description: An instantiation and keep-alive bash script for noaaportIngester\n"
  printf "    and blender for SBN data redundancy. Launches the noaaportIngest then\n"
  printf "    streams data through a named pipe (FIFO) to blender, such there is an\n"
  printf "    executing pair for each SBN data stream.\n"
  printf "  arguments:\n"
  printf "\t-b <log>\t\tLog file for blender, default: LDM logfile.\n"
  printf "\t-d\t\t\tShow arguments passed and full call for each executable without actually starting blender, default: OFF.\n"
  printf "\t-F <server(:port)>\tfanout server address (if port is missing, will use port declared by -p) [REQUIRED].\n"
  printf "\t\t\t\tCan have multiple -F declarations.\n"
  printf "\t-f <fifo>\t\tName of FIFO to create, default: /tmp/blender_<port>.fifo\n"
  printf "\t-l <log>\t\tLog file for noaaportIngester, default: LDM logfile.\n"
  printf "\t-p <port>\t\tfanout server port number, default: defined in -F per server.\n"
  printf "\t-R <rcvBuf>\t\tReceive buffer size in bytes, default: 2097152.\n"
  printf "\t-t <delay>\t\tfixed delay, in seconds, to avoid duplicate frames, default: 0.01.\n"
  printf "\t-v\t\t\tVerbose mode for blender and Debug mode (NOTICE) for noaaportIngester, default OFF.\n"
  printf "\t-x\t\t\tDebug mode for blender and Verbose mode (INFO) for noaaportIngester, default OFF.\n"
}

function initVariables
{
  bLogFile=""
  debug="0"
  fServers=()
  fIndex="0"
  Fifo=""
  nLogFile=""
  fPort=""
  rBuffer="2097152"
  rLag="0.01"
  bVerbose="0"
  nVerbose="0"
}

function testForAllArgsRequired
{
  if [ "${#fServers[@]}" -eq 0 ]; then
    echo "-1"
    return
  fi
  echo "${#fServers[@]}"
}

function buildBlenderCommand
{
  BLENDER="${BLENDER} -t ${rLag}"
  if [ "${bVerbose}" -ne 1 -a "${nVerbose}" -eq 1 ]; then
    BLENDER="${BLENDER} -x"
  fi
  if [ "${bVerbose}" -eq 1 ]; then
    BLENDER="${BLENDER} -v"
  fi
  BLENDER="${BLENDER} -R ${rBuffer}"
  if [ -n "${bLogFile}" ]; then
    BLENDER="${BLENDER} -l ${bLogFile}"
  fi
  BLENDER="${BLENDER} ${fServers[@]}"
  BLENDER=`printf "${BLENDER} > ${Fifo}"`
}

function buildNoaaportIngesterCommand
{
  if [ "${bVerbose}" -eq 1 -a "${nVerbose}" -ne 1 ]; then
    NOAAPORTINGESTER="${NOAAPORTINGESTER} -n"
  fi
  if [ "${nVerbose}" -eq 1 ]; then
    NOAAPORTINGESTER="${NOAAPORTINGESTER} -v"
  fi
  if [ -n "${nLogFile}" ]; then
    NOAAPORTINGESTER="${NOAAPORTINGESTER} -l ${nLogFile}"
  fi
  NOAAPORTINGESTER=`printf "${NOAAPORTINGESTER} < ${Fifo}"`
}

function showCommandsAndExit
{
  printf "noaaportIngester command:\n${NOAAPORTINGESTER}\n"
  printf "blender command:\n${BLENDER}\n"
  exit 0
}

function terminateProcess
{
  terminate="1"
}

# Internal function definitions END
## Main BEGIN

initVariables
trap "terminateProcess" TERM

while getopts "b:dF:f:l:p:R:t:vx" o
do
  case "${o}" in
    b)
      bLogFile="${OPTARG}"
      justFile=`echo "${bLogFile}" | awk -F"/" '{print $NF}'`
      justPath=`echo "${bLogFile}" | sed -e "s|/${justFile}$||g"`
      if [ -n "${justPath}" ]; then
        if [ ! -d "${justPath}" ]; then
          mkdir -p ${justPath}
          if [ ! -d "${justPath}" ]; then
            echo "ERROR: blender log path, ${justPath}, does not exist and cannot be created."
            echo "Exiting."
            exit -11
          fi
        fi
      fi
      ;;
    d)
      debug="1"
      ;;
    F)
      fServers[${fIndex}]="${OPTARG}"
      fIndex=`expr "${fIndex}" + 1`
      ;;
    f)
      Fifo="${OPTARG}"
      ;;
    l)
      nLogFile="${OPTARG}"
      justFile=`echo "${nLogFile}" | awk -F"/" '{print $NF}'`
      justPath=`echo "${nLogFile}" | sed -e "s|/${justFile}$||g"`
      if [ -n "${justPath}" ]; then
        if [ ! -d "${justPath}" ]; then
          mkdir -p ${justPath}
          if [ ! -d "${justPath}" ]; then
            echo "ERROR: noaaportIngester log path, ${justPath}, does not exist and cannot be created."
            echo "Exiting."
            exit -12
          fi
        fi
      fi
      ;;
    p)
      fPort=`echo "${OPTARG}" | grep -o -E "[0-9]*"`
      if [ -n "${fPort}" ]; then
        if [ "`echo \"${fPort} < 1024 || ${fPort} > 65535\" | bc`" -eq 1 ]; then
          fPort=""
        fi
      fi
      if [ -z "${fPort}" ]; then
        echo "ERROR: Port configured, ${OPTARG}, is not a valid port in the range of 1024 to 65535."
        echo "Exiting."
        exit -13
      fi
      ;;
    R)
      rBuffer=`echo "${OPTARG}" | grep -o -E "[0-9]*"`
      if [ -n "${rBuffer}" ]; then
        if [ "`echo \"${rBuffer} < 102400 || ${rBuffer} > 10240000\" | bc`" -eq 1 ]; then
          rBuffer=""
        fi
      fi
      if [ -z "${rBuffer}" ]; then
        echo "ERROR: Receive buffer configured, ${OPTARG}, is not within the valid range of 102400 to 10240000."
        echo "Exiting."
        exit -14
      fi
      ;;
    t)
      rLag=`echo "${OPTARG}" | grep -o -E "[0-9\.]*"`
      if [ -n "${rLag}" ]; then
        if [ "`echo \"${rLag} < 0.001 || ${rLag} > 600.0\" | bc -l`" -eq 1 ]; then
          rLag=""
        fi
      fi
      if [ -z "${rLag}" ]; then
        echo "ERROR: Frame delay configured, ${OPTARG}, is not within the valid range of 0.001 to 600."
        echo "Exiting."
        exit -15
      fi
      ;;
    v)
      bVerbose="1"
      ;;
    x)
      nVerbose="1"
      ;;
    *)
      printUsage
      ;;
  esac
done
shift $((OPTIND-1))

argTest=`testForAllArgsRequired`
if [ "${argTest}" -le 0 ]; then
  printUsage
  exit -1
fi

# Set named pipe (fifo)
if [ -z "${Fifo}" ]; then
  if [ -n "${fPort}" ]; then
    Fifo="/tmp/blender_${fPort}.fifo"
  else
    firstPort=`echo "${fServers[0]}" | grep ":" | awk -F":" '{print $NF}'`
    if [ -z "${firstPort}" ]; then
      echo "-2"
      return
    fi
    Fifo="/tmp/blender_${firstPort}.fifo"
  fi
fi

# Afix port per host on fanout server(s)
lclIndex="0"
while [ "${lclIndex}" -lt "${fIndex}" ]
do
  currentPort=`echo "${fServers[${lclIndex}]}" | grep ":" | awk -F":" '{print $NF}'`
  if [ -z "${currentPort}" ]; then
    if [ -n "${fPort}" ]; then
      fServers[${lclIndex}]="${fServers[${lclIndex}]}:${fPort}"
    else
      echo "-3"
      return
    fi
  fi
  lclIndex=`expr "${lclIndex}" + 1`
done

buildBlenderCommand
buildNoaaportIngesterCommand

if [ "${debug}" -eq 1 ]; then
  showCommandsAndExit
fi

${ULOGGER} -l ${thisScriptLog} "${0}:Starting"

while [ "${terminate}" -eq 0 ]
do
  rm -rf ${Fifo}
  mkfifo ${Fifo}
  if [ ! -e "${Fifo}" ]; then
    echo "Cannot create named pipe ${Fifo}, exiting"
    exit -5
  fi

  eval "${NOAAPORTINGESTER}" &
  npProcess="$!"
  eval "${BLENDER}" &
  blProcess="$!"
  wait ${npProcess} ${blProcess}
done
## Main END

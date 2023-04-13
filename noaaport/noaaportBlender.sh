set -e

function printHelp
{
  echo "${0} usage:"
  echo " -b <log>        Log file for blender, default: LDM logfile"
  echo " -f <fifo>       Name of FIFO to create, default: /tmp/blender_<port>.fifo"
  echo " -l <log>        Log file for noaaportIngester, default: LDM logfile"
  echo " -p <port>       fanout server port number"
  echo " -R <rcvBuf>  Receive buffer size in bytes"
  echo " -s <fanoutadd>  one or more fanoutServerAddresses with syntax:"
  echo "                         <server:port> ..."
  echo "                         If <port> is missing, option '-p <port>' is required,"
  echo " -t <delay>      fixed delay, in seconds, to avoid duplicate frames"
  echo " -v              Verbose mode for 'blender' and Debug mode (NOTICE) for 'noaaportIngester'"
  echo " -x              Debug mode for 'blender' and Verbose mode (INFO) for 'noaaportIngester'"
}

function initArgs
{
  BLLOGFILE=""
  FIFONAME=""
  NPLOGFILE=""
  PORTNUMBER=""
  BUFFERSIZE=""
  FANOUTADDRESSES=""
  TIMEOUTDELAY=""
  VERBOSE="0"
  DEBUGMODE="0"
}

function printArgs
{
  echo "Blender log file = ${BLLOGFILE}"
  echo "FIFO = ${FIFONAME}"
  echo "NOAAPort log file = ${NPLOGFILE}"
  echo "Fanout port number = ${PORTNUMBER}"
  echo "Receive buffer size = ${BUFFERSIZE}"
  echo "Fanout addresses = ${FANOUTADDRESSES[@]}"
  echo "Delay for checking duplicate frames = ${TIMEOUTDELAY}"
  echo "Verbose = ${VERBOSE}"
  echo "Debug = ${DEBUGMODE}"
}

initArgs

while getopts ":b:f:l:p:R:s:t:vx" options
do
  case "${options}" in
    b)
      BLLOGFILE=${OPTARG}
      ;;
    f)
      FIFONAME=${OPTARG}
      ;;
    l)
      NPLOGFILE=${OPTARG}
      ;;
    p)
      PORTNUMBER=${OPTARG}
      ;;
    R)
      BUFFERSIZE=${OPTARG}
      ;;
    s)
## Need to debug how a string with spaces is dealt with
      FANOUTADDRESSES=(${OPTARG})
      ;;
    t)
      TIMEOUTDELAY=${OPTARG}
      ;;
    v)
      VERBOSE="1"
      ;;
    x)
      DEBUGMODE="1"
      ;;
    :)
      echo "Error: -${OPTARG} requires an argument."
      exit -2
      ;;
    *)
      printHelp
      exit 0
      ;;
  esac
done

printArgs

checkForRunning

initalizeInstances

trap "clearInstances" SIGTERM

while (true)
do
  wait
  clearInstances
  initalizeInstances
done

clearInstances
     
#!/bin/sh

#--------------------------------------------------------------------------
#
# Name:    ndfdprocess.sh
#
# Purpose: extract individual GRIB2 messages from test NDFD product files;
#          create useful Product IDs; and insert them into an LDM queue
#
# Note:    modify the 'LOG' file to suit your needs
#
# History: 20100507 - Created for NDFD testing
#          20100528 - Add 'case' to eliminate processing of select products
#          20100528 - Set 'gribinsert' log level to notice only ('-l-')
#
#--------------------------------------------------------------------------

SHELL=sh
export SHELL

# Metadata from original product
wmo=$1
center=$2
day=$3
hhmm=$4
dir=$5
feed=$6

# Send all messages to a log file
logfile=logs/ndfdprocess.log
exec >>$logfile 2>&1

# Create output directory
mkdir -p $dir >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo $program "ERROR: unable to create directory $dir"
  cat > /dev/null
  exit
fi

# Throw away the "uninteresting" products
case "$wmo" in
	L[ABCDE]UZ9[78])
		echo Discarding $wmo $center ${day}$hhmm ...
		cat > /dev/null
		exit 0
		;;
	LFUZ97)
		echo Discarding $wmo $center ${day}$hhmm ...
		cat > /dev/null
		exit 0
		;;
	L[GHIJ]UZ98)
		echo Discarding $wmo $center ${day}$hhmm ...
		cat > /dev/null
		exit 0
		;;
	ZBUZ9[78])
		echo Discarding $wmo $center ${day}$hhmm ...
		cat > /dev/null
		exit 0
		;;
esac

# Create output file name
fname=${wmo}_${center}_${day}${hhmm}.grib2
pathname=$dir/$fname

# Write the log message and output
echo Writing $pathname
cat > $pathname

# Process the file just written with 'gribinsert'
bin/gribinsert -l- -q data/ldm.pq -f $feed -S $pathname

# Clean-up
echo Deleting $pathname
rm -f $pathname

# Done
exit 0

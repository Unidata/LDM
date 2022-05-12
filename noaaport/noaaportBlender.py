#!/usr/bin/env python3

###############################################################################
#
#
# Description: This Python script provides a command line interface to 
#  				the blender program. It is invoked as an executabe script.
#
#
#   @file:  noaaportBlender.py
# @author:  Mustapha Iles
#
#    Copyright 2021 University Corporation for Atmospheric Research
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# 
#
#  The $LDMHOME has to be set.
#
###############################################################################

"""
Generic script: 
---------------

-f <FIFO_name> use default [/tmp/blender_1201] - overwritten by an option -f <FIFO_name>
 (log default: LDM's)

noaaportBlender.py -b <log_blender> -n <log_noaaport> -f /tmp/myFIFO --fanout <fanoutAddress_1:1201> <fanoutAddress_2:1201> <fanoutAddress_3:1201>

noaaportBlender.py -b <log_blender> -n <log_noaaport>  --fanout  <fanoutAddress_4:1202> <fanoutAddress_5:1202> <fanoutAddress_6:1202>

Inside the script, create a loop to wait for the current pipelined processes to die and then resurrect them.

"""

import 	os, signal, os.path
import 	sys
import 	subprocess
import 	psutil
import 	time
from 	os 		import environ, system
from 	pathlib import Path
import  argparse
import 	getopt
import 	logging
from 	multiprocessing  import Process


class NoaaportBlender():

	"""
		Unidata NOAAPort fanout server:
		chico.unidata.ucar.edu <-> 128.117.140.37
		Ports: 1201 - 1210, inclusive

Here is, for instance, the listing from ~ldm/etc/ldmd.conf on leno:

http://www.nws.noaa.gov/noaaport/document/Multicast%20Addresses%201.0.pdf
            CHANNEL PID MULTICAST ADDRESS Port DETAILS
            NMC     101     224.0.1.1     1201 NCEP / NWSTG
            GOES    102     224.0.1.2     1202 GOES / NESDIS
            NMC2    103     224.0.1.3     1203 NCEP / NWSTG2
            NOPT    104     224.0.1.4     1204 Optional Data - OCONUS Imagery / Model
            NPP     105     224.0.1.5     1205 National Polar-Orbiting Partnership / POLARSAT
            ???     ???     224.0.1.6     1206 National Blend of Models
            EXP     106     224.0.1.8     1208 Experimental
            GRW     107     224.0.1.9     1209 GOES-R Series West
            GRE     108     224.0.1.10    1210 GOES-R Series East
            NWWS    201     224.1.1.1     1201 Weather Wire


 # NOAAPort DVB-S ingest
 EXEC "keep_running noaaportIngester -m 224.0.1.1  -l var/logs/nwstg.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.2  -l var/logs/goes.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.3  -l var/logs/nwstg2.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.4  -l var/logs/oconus.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.5  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.6  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.7  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.8  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.9  -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.0.1.10 -l var/logs/nother.log"
 EXEC "keep_running noaaportIngester -m 224.1.1.1  -l var/logs/wxwire.log"

	"""

	noaaportLogFile 	= None	#"/tmp/noaaport_"
	blenderLogFile  	= None	#"/tmp/blender_"
	thisScriptLog  		= "/tmp/noaaportBlender.log"
	port 				= ""	# nwstg = 1201, etc.
	

	# Replace destination with desired FIFO destination directory.
	FIFO_name = "/tmp/blender_"

	# blender  args
	# blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_out.log 1201 leno, chico"
	timeOut 		= "5.0"

	progUsage = {
		"progName": "noaaportBlender.py",
		"description":'''This file is the noaaportBlender script that 
					   launches both noaaportIngester and the blender 
					   communicating through a FIFO. The latter is created here
					   for each type of feed.''',
		"usage": '''\n\n\tnoaaportBlender.py [-x][-b <blender log>][-l <noaaportIngester log>][-f <fifo>][-p <port>][-t <timeOut>] --fanout <fanout>[:<port>] ...  \n
			
			-b <log>	Log file for blender, default: LDM logfile
			-f <fifo>	Name of FIFO to create, default: /tmp/blender_<port>.fifo
			-l <log>	Log file for noaaportIngester, default: LDM logfile
			-p <port>	fanout server port number
			-R <rcvBuf>  Receive buffer size in bytes
			-t <delay>	fixed delay, in seconds, to avoid duplicate frames
			-v 		Verbose mode for 'blender' and Debug mode (NOTICE) for `noaaportIngester`
			-x 		Debug mode for `blender` and Verbose mode (INFO) for 'noaaportIngester'
			--fanout 	one or more fanoutServerAddresses with syntax: 
						<server:port> ... 
						If <port> is missing, option '-p <port>' is required,
			''',
		"epilog":'''
		Thank you for using noaaportBlender.py...
				'''	
	}

	def __init__(self):	


		ldmHome     = environ.get("LDMHOME", "nowhere")
		ldmPath 	= Path(ldmHome)
		if ldmHome == "nowhere" or not os.path.isdir(ldmPath):

			msg = "The LDMHOME environment variable is NOT set. Bailing out."
			self.ulogIt(msg, "__init__", 151)
			exit(2)

		# Check CLI options:
		self.cliParserInit = argparse.ArgumentParser(
			prog 		= self.progUsage["progName"],
			description = self.progUsage["description"],
			usage 		= self.progUsage["usage"],
			epilog 		= self.progUsage["epilog"]
			)


	def usage(self):
		program		= self.progUsage["progName"]
		description = self.progUsage["description"]
		usage 		= self.progUsage["usage"]
		epilog 		= self.progUsage["epilog"]

		self.ulogIt(usage, "usage", 171)
		self.ulogIt(epilog, "usage", 172)

		sys.exit(2)


	def prepareBlenderCmd(self, cliArgs):

		blenderArgs = f" -t {self.timeOut} "
		if cliArgs["timeOut"] != None:	
			timeOut = cliArgs["timeOut"]
			blenderArgs = f" -t {timeOut} "

		# debug has precedence over verbose

		if cliArgs["verbose"] == True and cliArgs["debug"] == False:	
			blenderArgs += " -v "

		if cliArgs["debug"] == True:
			blenderArgs += " -x "

		if cliArgs["rcvBufSize"] != None:
			rcvBufSize = cliArgs["rcvBufSize"]
			blenderArgs += f" -R {rcvBufSize} "

		# "blender -t 0.01  -l /var/logs/wxwire.log  chico:1201"
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1205  chico:1205
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1206

		if self.blenderLogFile != None:
			blenderArgs    += f" -l {self.blenderLogFile} "

		for hostId in cliArgs["fanoutServerAddresses"]:
			blenderArgs    += f" {hostId} "

		blenderArgs    += f"  > {self.FIFO_name}"
		blenderCmd 		= f"blender {blenderArgs} "

		# Build the blender command ------------------------------------
		return blenderCmd


	def buildLogFilesAndFifo(self, cliArgs):

		# Capture the feedType port # first if option exists
		if cliArgs["feedTypePort"] != None:
			self.port = cliArgs["feedTypePort"]
		else: # extract it from fanout server address
			if len(cliArgs["fanoutServerAddresses"][0].split(':')) != 2:
				errorMsg = "\n\n\t\t<port> is missing\n\n"
				self.ulogIt(errorMsg, "buildLogFilesAndFifo", 207)
				self.usage()

			self.port = cliArgs["fanoutServerAddresses"][0].split(':')[1]


		noaaportLogFile = cliArgs["noaaportLogFile"]
		if noaaportLogFile != None:
			self.noaaportLogFile = noaaportLogFile
		
		# If blender logfile not provided, use default? or LDM's?
		blenderLogFile = cliArgs["blenderLogFile"]
		if cliArgs["blenderLogFile"] != None:	
			self.blenderLogFile = blenderLogFile
		
		# FIFO
		self.FIFO_name = self.makePipe(self.port, cliArgs)


	def makePipe(self, feedTypePort, cliArgs):

		pipeName = cliArgs["fifoName"] 	# <- use option
		if pipeName == None:			# <- use default
			pipeName	= f"{self.FIFO_name}{feedTypePort}.fifo"
		pipePath 	= Path(pipeName)
		if not os.path.exists(pipeName):
			cmd_proc= f"mkfifo {pipeName}"
			try:
				proc= subprocess.check_output(cmd_proc, shell=True )		

				return pipeName

			except Exception as e:
				# self.errmsg(f"{cmd} is currently NOT running! ")
				errmsg = f"Could not create FIFO with this path name: {pipeName}"
				self.ulogIt(errMsg, "makePipe", 242)
				sys.exit(2)

		return pipeName


	# Build the blender's command line arguments
	def prepareNoaaportCmd(self, cliArgs):

		noaaportArgs 	= " "
		# debug has precedence over verbose
		if cliArgs["verbose"] == True and cliArgs["debug"] == False:	
			noaaportArgs 	+= " -n "

		if cliArgs["debug"] == True:	
			noaaportArgs 	+= " -v "	# that's how noaaportingester is expecting it...

		if self.noaaportLogFile != None:
			noaaportArgs 	+= f" -l {self.noaaportLogFile} "

		noaaportCmd 	= f"noaaportIngester  {noaaportArgs} < {self.FIFO_name} "

		return noaaportCmd
	

	def cliParser(self):

		self.cliParserInit.add_argument('-b', dest='blenderLogFile', action="store", help='Default: LDM logfile', required=False)
		self.cliParserInit.add_argument('-f', dest='fifoName', action="store", help='Default: /tmp/blender_1201.fifo', required=False)
		self.cliParserInit.add_argument('-l', dest='noaaportLogFile', action="store",help='Default: LDM logfile', required=False)
		self.cliParserInit.add_argument('-p', dest='feedTypePort', action="store", help='', required=False)
		self.cliParserInit.add_argument('-R', dest='rcvBufSize', action="store", help='', required=False)
		self.cliParserInit.add_argument('-t', dest='timeOut', action="store", help='Default: 0.01', required=False)
		self.cliParserInit.add_argument('-v', dest='verbose', action="store_true",help='', required=False)
		self.cliParserInit.add_argument('-x', dest='debug', action='store_true', help='', required=False)
		self.cliParserInit.add_argument('--fanout', dest='fanoutServerAddresses', 
			action="store", help='', required=True, metavar='fanoutAddress', type=str, nargs='+')

		args, other = self.cliParserInit.parse_known_args()
		
		return vars(args)     # vars(): converts namespace to dict
		

	def ulogIt(self, msg, funcName, lineNum):

		#cmd = f'ulogger  -l /tmp/noaaportBlender.log "noaaportBlender.py:{funcName}:{lineNum}  {msg}"'
		cmd = f'ulogger -l {self.thisScriptLog} "noaaportBlender.py:{funcName}:{lineNum}  {msg}"'
		self.runProc(cmd) 


	def runProc(self, cmd):

		try:
			proc = subprocess.check_call(cmd, shell=True )
			
		except Exception as e:
			if cmd.startswith("ulog"): # avoid looping
				print(cmd)
			else:
				self.ulogIt(f"{e}", "runProc", 308)
				exit(0)

	def execIngesterOrBlender(self, cmd, progName):
		msg = f"re-Starting {progName}..."
		self.ulogIt(msg, "execIngesterOrBlender", 313)
		system(cmd)

def main():

	system('clear')
	
	noaaBPInst 		= NoaaportBlender()	

	startMsg = f"Starting up the 2 programs (blender and noaaportIngester)..."
	noaaBPInst.ulogIt( startMsg , "main", 323)

	cliArg 			= noaaBPInst.cliParser()	# for this script
	noaaBPInst.buildLogFilesAndFifo(cliArg)

	noaaportCmd 	= noaaBPInst.prepareNoaaportCmd(cliArg)
	blenderCmd 		= noaaBPInst.prepareBlenderCmd(cliArg)

	while True:

		noaaBPInst.ulogIt(noaaportCmd, "main", 333)
		noaaBPInst.ulogIt(blenderCmd, "main", 334)
		
		noaaProc 	= Process(target=noaaBPInst.execIngesterOrBlender, args=(noaaportCmd, "NOAAport Ingester", ))
		blenderProc = Process(target=noaaBPInst.execIngesterOrBlender, args=(blenderCmd,  "blender", ))

		noaaProc.start()		# <-- reader first	
		blenderProc.start()
		
		
		noaaProc.join()       	# <-- reader first
		blenderProc.join()    	# <-- writer
		
		msg = "The NOAAport ingester and the blender have stopped. Re-running them..."
		noaaBPInst.ulogIt(msg, "main", 346)
		time.sleep(1)	


if __name__ == '__main__':

	main()

#!/usr/bin/env python3

###############################################################################
#
#
# Description: This Python script provides a command line interface to 
#  				the blender program. It is invoked as an executabe script.
#
#
#   @file:  noaaportBlenderPipe
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
Generic script: -f <FIFO_name> use default [/tmp/blender_1201] - overwritten by an option -f <FIFO_name>
----------------  (log default: LDM's)
noaaportBlenderLauncher.py -b <log_blender> -n <log_noaaport>  <fanoutAddress_1:1201> <fanoutAddress_2:1201> <fanoutAddress_3:1201>

noaaportBlenderLauncher.py -b <log_blender> -n <log_noaaport>  <fanoutAddress_4:1202> <fanoutAddress_5:1202> <fanoutAddress_6:1202>

Inside the script, create a loop to wait for the current pipelined processes to die and then resurrect them.



Test script:
------------
noaaportBlenderLauncher.py 		(all arguments are embedded in the script itself for the user to change)


"""



import 	os, signal, os.path
import 	sys
import 	subprocess
import 	psutil
import 	time
from 	os 	import environ, system
from 	pathlib import Path
import  argparse


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

	noaaportLogFile = "/tmp/noaaport_"
	blenderLogFile  = "/tmp/blender_"
	port 	= ""	# nwstg = 1201, etc.

	# 1. for testing purposes. Enabled below feedType when ready
	noaaportExecLines = {
		"nwstg":		"/tmp/nwstg.log"	
	}
	"""
		"nwstg":		"/var/logs/nwstg.log"
		,
		"goes": 		"/var/logs/goes.log"
		,
 		"nwstg2": 		"/var/logs/nwstg2.log"
 		,
 		"oconus": 		"/var/logs/oconus.log"
 		,
		"nother": 		"/var/logs/nother.log"
		,
 		"wxwire":		"/var/logs/wxwire.log"
 		
 	}
	"""
	
	# 2. for testing purposes. Enabled below fanout server multicast IP (chico only?) when ready
	# multicast IP address
	fanoutServerAddress = "localhost:1201" 	#chico.unidata.ucar.edu:1201"	# <-> 128.117.140.37"

	# 3. for testing purposes. Replace destination with desired FIFO destination directory.
	FIFO_name = "/tmp/blender_"

	# blender  args
	# blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_out.log 1201 leno, chico"
	timeOut 		= "0.01"

	def __init__(self):	
		
		ldmHome     		= environ.get("LDMHOME", "/home/miles/projects/ldm")	# remove 'miles' path
		self.noaaportPath	= f"{ldmHome}/src/noaaport"

		# Check CLI options:
		self.cliParserInit = argparse.ArgumentParser(
			prog='noaaportBlender',
			description='''This file is the noaaportBlender script that 
						   launches both noaaportIngester and the blender 
						   communicating through a FIFO. The latter is created here
						   for each type of feed.
						''',
			usage='''\n\n\t%(prog)s [x][-b <blender log>][-n <noaaportIngester log>] [-p <port#>] --fanout <fanout>:<port> ...  \n
			-x 			Debug mode
			-b <log>	Log file for blender, default: LDM logfile
			-n <log>	Log file for noaaportIngester, default: LDM logfile
			-p <port>	fanout server port number
			--fanout 	one or more fanoutServerAddresses with syntax: 
							<server:port> ...
			''',
			epilog='''
				Thank you for using %(prog)s...
					'''
			)


	def prepareBlenderCmd(self, cliArg):

		blenderArgs = f" -t {self.timeOut} "
		if cliArg["debugMode"] == True:
			blenderArgs += " -x "

		# "blender -t 0.01  -l /var/logs/wxwire.log  chico:1201"
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1205  chico:1205
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1206

		blenderArgs    += f" -l {self.blenderLogFile} "

		for hostId in cliArg["fanoutServerAddresses"]:
			blenderArgs    += f" {hostId} "

		blenderArgs    += f"  > {self.FIFO_name}"
		blenderCmd 		= f"{self.noaaportPath}/blender {blenderArgs} &"

		# Build the blender command ------------------------------------
		return blenderCmd


	def buildLogFilesAndFifo(self, cliArgs):

		# Capture the feedType port # first if option exists
		if cliArgs["feedTypePort"] != None:
			self.port = cliArgs["feedTypePort"]
		else: # extract it from fanout server address
			self.port = cliArgs["fanoutServerAddresses"][0].split(':')[1]


		noaaportLogFile = cliArgs["noaaportLogFile"]
		if noaaportLogFile != None:
			self.noaaportLogFile = noaaportLogFile
		else:
			self.noaaportLogFile += f"{ self.port}.log"
		
		# If blender logfile not provided, use default? or LDM's?
		blenderLogFile = cliArgs["blenderLogFile"]
		if cliArgs["blenderLogFile"] != None:	
			self.blenderLogFile = blenderLogFile
		else:
			self.blenderLogFile += f"{self.port}.log"

		# FIFO
		self.FIFO_name = self.makePipe(self.port)


	# Build the blender's command line arguments
	def prepareNoaaportCmd(self, cliArgs):

		noaaportArgs 	= f" -l {self.noaaportLogFile} < {self.FIFO_name}"
		noaaportCmd 	= f"{self.noaaportPath}/noaaportIngester     {noaaportArgs} &"

		return noaaportCmd


	# Build the blender's command line arguments
	# For testing
	def prepareTestNoaaportCmdsList(self, cliArgs):


		noaaportCmdsList = []


		for feedType, logFile in self.noaaportExecLines.items():

			# Make the pipeline:
			pipeline 	= self.makePipe(feedType)
			if pipeline == None:
				print(f"ERROR: makePipe(feedType={feedType}) FAILED! ")
				exit()

			noaaportArgs 	= f" -l {logFile} < {pipeline}"
			noaaportCmd 	= f"{self.noaaportPath}/noaaportIngester     {noaaportArgs} &"

			noaaportCmdsList.append(noaaportCmd)

		return noaaportCmdsList;



	def prepareTestBlenderCmdsList(self, cliArg):

		blenderCmdsList = []

		for feedType, logFile in self.noaaportExecLines.items():

			blenderArgs = f" -t {self.timeOut} "
			if cliArg["debugMode"] == True:
				blenderArgs += " -x "

		# "blender -t 0.01  -l /var/logs/wxwire.log  chico:1201"
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1205  chico:1205
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1206

			samePipeline 	= f"{self.FIFO_name}/{feedType}"
			blenderArgs    += f" -l {logFile} {self.fanoutServerAddress} > {samePipeline}"
		
			blenderCmd 		= f"{self.noaaportPath}/blender {blenderArgs} &"

			blenderCmdsList.append(blenderCmd)

		# Build the blender command ------------------------------------
		return blenderCmdsList




	def makePipe(self, feedTypePort):

		pipeName	= f"{self.FIFO_name}{feedTypePort}.fifo"
		pipePath = Path(pipeName)
		if not os.path.exists(pipeName):
			
			cmd_proc	= f"mkfifo {pipeName}"
			try:
				proc = subprocess.check_output(cmd_proc, shell=True )
				#for line in proc.decode().splitlines():
				print(f"FIFO: {pipeName} created.")			

				return pipeName

			except Exception as e:
				# PRINT NOTHING
				# self.errmsg(f"{cmd} is currently NOT running! ")
				pass

		return pipeName
	
	
	def executeNoaaportIngester(self, cmd):

		print(cmd)		
#		system( cmd )


	# Execute the blender with arguments gleaned from user
	def executeBlender(self, cmd):
		# Check if program(s) are running. If so, kill them
		#self.checkRunning()

		print(cmd)

		#system( cmd )


	# Check if testBlender or blender processes are still running: kill them
	def checkRunning(self):		

		# remove *blender process
		blender_ps = f"ps -ef | grep -v grep | grep -i blender | grep -v blenderAdmin"
		self.checkProcessAndKill(blender_ps, "blender")


	# Clean up previous runs of the blender
	def checkProcessAndKill(self, cmd_proc, cmd):

		try:
			proc = subprocess.check_output(cmd_proc, shell=True )
			for line in proc.decode().splitlines():
				
				procId = line.split()[1]
				p = psutil.Process(int(procId))

				go = input(f"\n\t\tKill process this process? \n\n\t\t---> {line}\n\n\t\t\t\tYes/No (Y/n)\n\t\t\t\t")
				if 'Y' == go.rstrip():
					pass #p.kill()
			

		except Exception as e:
			# PRINT NOTHING
			# self.errmsg(f"{cmd} is currently NOT running! ")
			pass
				

	def runProc(self, cmd):

		print("Entering runProc")
		try:
			proc = subprocess.check_call(cmd, shell=True )
			
			print(proc)
			for line in proc.decode().splitlines():
				print(line)
				procId = line.split()[1]
				p = psutil.Process(int(procId))
				
				print(procId)

			print("Exiting runProc")

			return procId
			"""

			for line in proc.decode().splitlines():
				
				procId = line.split()[1]
				p = psutil.Process(int(procId))

				go = input(f"\n\t\tKill process this process? \n\n\t\t---> {line}\n\n\t\t\t\tYes/No (Y/n)\n\t\t\t\t")
				if 'Y' == go.rstrip():
					pass #p.kill()
			"""

		except Exception as e:
			# PRINT NOTHING
			# self.errmsg(f"{cmd} is currently NOT running! ")

			print("Exiting runProc")



	# Parse this script's command line
	def cliParser(self):

		self.cliParserInit.add_argument('-x', dest='debugMode', action='store_true', help='', required=False)
		self.cliParserInit.add_argument('-b', dest='blenderLogFile', action="store", help='default: LDM logfile', required=False)
		self.cliParserInit.add_argument('-n', dest='noaaportLogFile', action="store",help='default: LDM logfile', required=False)
		self.cliParserInit.add_argument('-p', dest='feedTypePort', action="store", help='', required=False)
		self.cliParserInit.add_argument('--fanout', dest='fanoutServerAddresses', 
			action="store", help='', required=True, metavar='fanoutAddress', type=str, nargs='+')

		args, other = self.cliParserInit.parse_known_args()
		
		return vars(args)     # vars(): converts namespace to dict
		

	def errmsg(self, msg):
		print(f"\n\tNote: {msg}")


def main():

	system('clear')
	
	noaaBPInst 		= NoaaportBlender()	

	cliArg 			= noaaBPInst.cliParser()	# for this script
	noaaBPInst.buildLogFilesAndFifo(cliArg)
	noaaportCmd 	= noaaBPInst.prepareNoaaportCmd(cliArg)
	blenderCmd 		= noaaBPInst.prepareBlenderCmd(cliArg)
	
	while true:

		print(noaaportCmd)
		print(blenderCmd)

		nooaProc 	= subprocess.Popen(noaaportCmd, stdout=subprocess.PIPE, shell=True)
		blenderProc = subprocess.Popen(noaaportCmd, stdout=subprocess.PIPE, shell=True)
		
		noaaProc.wait()
		blenderProc.wait()

		print(f"Re-running... (loop) ")
	


if __name__ == '__main__':

	main()
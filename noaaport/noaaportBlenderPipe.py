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

	noaaportExecLines = {
		"nwstg":		"/var/logs/nwstg.log",
		"goes": 		"/var/logs/goes.log",
 		"nwstg2": 		"/var/logs/nwstg2.log",
 		"oconus": 		"/var/logs/oconus.log",
		"nother": 		"/var/logs/nother.log",
 		"wxwire":		"/var/logs/wxwire.log"
 	}
	
	# blender  args
	# blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_out.log 1201 leno, chico"
	timeOut 		= "0.01"

	# multicast IP address
	multicastIP = "chico.unidata.ucar.edu"	# <-> 128.117.140.37"
	DESTINATION = "/tmp"

	def __init__(self):	
		
		ldmHome     		= environ.get("LDMHOME", "/home/miles/projects/ldm")	# remove 'miles' path
		self.noaaportPath	= f"{ldmHome}/src/noaaport"

		# Check CLI options:
		self.cliParserInit = argparse.ArgumentParser(
			prog='blenderAdmin',
			description='''This file is the noaaportBlenderPipe script that 
						   is launches both noaaportIngester and the blender 
						   communicating through a pipe created here
						   for each type of feed.
						''',
			usage='''\n\n\t%(prog)s [-l logFile] ...  \n
			-l <log>	Log file (default: /tmp/blender.log)
			-x 		Debug mode

			''',
			epilog='''
				Thank you for using %(prog)s...
					'''
			)


	def prepareBlenderCmdsList(self, cliArg):

		blenderCmdsList = []

		for feedType, logFile in self.noaaportExecLines.items():

			blenderArgs = f" -t {self.timeOut} "
			if cliArg["debugMode"] == True:
				blenderArgs += " -x "

		# "blender -t 0.01  -l /var/logs/wxwire.log  chico:1201"
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1205
		#  /home/miles/projects/ldm/src/noaaport/.libs/lt-blender -t 0.01 -x -l /tmp/blender.log localhost:1206

			samePipeline 	= f"{self.DESTINATION}/{feedType}"
			blenderArgs    += f" -l {logFile} > {samePipeline}"
		
			blenderCmd 		= f"{self.noaaportPath}/blender {blenderArgs} &"
			print(blenderCmd)

			blenderCmdsList.append(blenderCmd)

		# Build the blender command ------------------------------------
		return blenderCmdsList



	# Build the blender's command line arguments
	def prepareNoaaportCmdsList(self):

		noaaportCmdsList = []
		for feedType, logFile in self.noaaportExecLines.items():

			# Make the pipeline:
			pipeline 	= self.makePipe(feedType)
			if pipeline == None:
				print(f"ERROR: makePipe(feedType={feedType}) FAILED! ")
				exit()

			noaaportArgs 	= f" -l {logFile} < {pipeline}"
			noaaportCmd 	= f"  {self.noaaportPath}/noaaportIngester  {noaaportArgs} &"

			noaaportCmdsList.append(noaaportCmd)

		return noaaportCmdsList;


	def makePipe(self, feedType):

		pipeName	= f"{self.DESTINATION}/{feedType}"
		pipePath = Path(pipeName)
		if not os.path.exists(pipeName):
			
			cmd_proc	= f"mkfifo {pipeName}"
			try:
				proc = subprocess.check_output(cmd_proc, shell=True )
				#for line in proc.decode().splitlines():
				#	print(line)			

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
				
	# Parse this script's command line
	def cliParser(self):
		"""
		
		self.cliParserInit.add_argument('-r', dest='redirectOutput', action='store_true', help='', required=False)
		"""
		self.cliParserInit.add_argument('-x', dest='debugMode', action='store_true', help='', required=False)
		self.cliParserInit.add_argument('-l', dest='logFile', action="store", help='', required=False)
		
		args, other = self.cliParserInit.parse_known_args()
		
		return vars(args)     # vars(): converts namespace to dict
		

	def errmsg(self, msg):
		print(f"\n\tNote: {msg}")


def main():

	system('clear')
	
	noaaBPInst 		= NoaaportBlender()	
	ingestCmdList 	= noaaBPInst.prepareNoaaportCmdsList()

	cliArg 			= noaaBPInst.cliParser()
	blendCmdList 	= noaaBPInst.prepareBlenderCmdsList(cliArg)
	
	for entry in range(0, len(noaaBPInst.noaaportExecLines)):
		print("---------------------------")
		noaaBPInst.executeNoaaportIngester( ingestCmdList[entry] )	
		noaaBPInst.executeBlender(blendCmdList[entry])

if __name__ == '__main__':

	main()
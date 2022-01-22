#!/usr/bin/env python3

###############################################################################
#
#
# Description: This Python script provides a command line interface to 
#  				the blender program. It is invoked as an executabe script.
#
#
#   @file:  blenderAdmin
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

import 	os, signal
import 	sys
import 	subprocess
import 	psutil
import 	time
from 	os 	import environ, system

class Blender():
	"""
		Unidata NOAAPort fanout server:
		chico.unidata.ucar.edu <-> 128.117.140.37
		Ports: 1201 - 1210, inclusive

Here, for instance is the listing
from ~ldm/etc/ldmd.conf on leno:

# 20170313 - changed set of noaaportIngester instances to match:
# http://www.nws.noaa.gov/noaaport/document/Multicast%20Addresses%201.0.pdf
#            CHANNEL PID MULTICAST ADDRESS Port DETAILS
#            NMC     101     224.0.1.1     1201 NCEP / NWSTG
#            GOES    102     224.0.1.2     1202 GOES / NESDIS
#            NMC2    103     224.0.1.3     1203 NCEP / NWSTG2
#            NOPT    104     224.0.1.4     1204 Optional Data - OCONUS Imagery / Model
#            NPP     105     224.0.1.5     1205 National Polar-Orbiting Partnership / POLARSAT
#            ???     ???     224.0.1.6     1206 National Blend of Models
#            EXP     106     224.0.1.8     1208 Experimental
#            GRW     107     224.0.1.9     1209 GOES-R Series West
#            GRE     108     224.0.1.10    1210 GOES-R Series East
#            NWWS    201     224.1.1.1     1201 Weather Wire

	"""
	# socat(s) ----------------------------------------
	localhostServers = {
		"socat_1": 				"localhost:9127",	
		"socat_2": 				"localhost:9128"
	}
	socatServers = {
		"socat_1": 				"128.117.140.37:1201",
		"socat_2": 				"128.117.140.37:1202",
		"socat_3": 				"128.117.140.37:1203",
		"socat_4": 				"128.117.140.37:1204",
		"socat_5": 				"128.117.140.37:1205",
		"socat_6": 				"128.117.140.37:1206",
		"socat_7": 				"128.117.140.37:1207",
		"socat_8": 				"128.117.140.37:1208",
		"socat_9": 				"128.117.140.37:1209",
		"socat_10":				"128.117.140.37:1210"
	}

	whichServers = localhostServers
	nbrFrames	= 5
	nbrRuns		= 6
	runAndWait	= 2000000
	snoozeTime	= 1

	#5   6  2000000   1       9127"
	socatArgs 		= f" {nbrFrames} {nbrRuns} {runAndWait} {snoozeTime} " 
	socatLaunchers  = []

	# blender -------------------------------------------
	timeOut 		= "0.01"
	logFile 		= "/tmp/blender.log"
	debugMode 		= " -x "
	blenderArgs 	= f" {debugMode} -t {timeOut} -l {logFile} "
	
	#blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_2.log localhost:9127 localhost:9128"
	blenderLauncher	= ""
	socatCmd		= "testBlender"

	def __init__(self):	
		
		ldmHome     		= environ.get("LDMHOME", "/home/miles/projects/ldm")	# remove 'miles' path
		self.noaaportPath	= f"{ldmHome}/src/noaaport"
		socatList 			= ""

		# Build testBlender commands for each socat --------------------
		#for socat, hostId in self.socatServers.items():
		for socat, hostId in self.whichServers.items():
			port = hostId.split(":")[1]			

			self.socatLaunchers.append( f"testBlender {self.socatArgs} {port}"   )
			socatList += hostId + " "

		# Build the blender command ------------------------------------
		self.blenderLauncher = f"{self.noaaportPath}/blender {self.blenderArgs} {socatList} >/dev/null &"

		# Check if program(s) are running. If so, kill them
		self.checkRunning()
		system('clear')


	def execute(self):

		# 1. Start the socat(s)
		#for socatIdx in range(len(self.socatServers)):
		for socatIdx in range(len(self.whichServers)):
			socatLaunch = f"{self.noaaportPath}/{self.socatLaunchers[ socatIdx ]} > {self.logFile}  &"

			system( socatLaunch )
	
		# 2. Start the blender
		# (Use subprogram to control the status instead of os.system)
		system(self.blenderLauncher )


	# Check if testBlender or blender processes are still running: kill them
	def checkRunning(self):		

		# remove *blender process
		blender_ps = f"ps -ef | grep -v grep | grep -i blender | grep -v blenderAdmin"
		self.checkProcessAndKill(blender_ps, "blender")


	def checkProcessAndKill(self, cmd_proc, cmd):

		try:
			proc = subprocess.check_output(cmd_proc, shell=True )
			for line in proc.decode().splitlines():
				
				procId = line.split()[1]
				p = psutil.Process(int(procId))
				p.kill()

		except Exception as e:
			self.errmsg(f"{cmd} is currently NOT running! ")
			

	def errmsg(self, msg):
		print(f"\n\tNote: {msg}")


def main():

	system('clear')
	
	# Production
	blenderInst = Blender()	
	blenderInst.execute()


if __name__ == '__main__':

	main()
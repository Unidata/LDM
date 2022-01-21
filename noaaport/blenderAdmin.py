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

import 	os
from 	os       import environ, system
import 	sys
import 	readline

from signal import signal, SIGINT
from time 	import sleep

testing = True

class TestBlender():

	# socat(s) ----------------------------------------
	socatServers={
		"socat_1": 				"localhost:9127",
		"socat_2": 				"localhost:9128"
	}

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
	skipOption		= " -s "
	blenderArgs 	= f" -t {timeOut} -l {logFile} "
	#blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_2.log localhost:9127 localhost:9128"
	blenderLauncher	= ""

	def __init__(self, testValue):	
		
		self.test = testValue
		ldmHome     = environ.get("LDMHOME", "/home/miles/projects/ldm")
		self.noaaportPath	= f"{ldmHome}/src/noaaport"
		
		socatList 			= ""

		# Build testBlender commands for each socat --------------------
		for socat, hostId in self.socatServers.items():
			port = hostId.split(":")[1]			

			self.socatLaunchers.append( f"testBlender {self.socatArgs} {port}"   )
			socatList += hostId + " "

		# Build the blender command ------------------------------------
		if self.test:
			self.blenderArgs += self.skipOption
		self.blenderLauncher = f"{self.noaaportPath}/blender {self.blenderArgs} {socatList} &"


	def execute(self):

		# 1. Start the socat(s)
		for socatIdx in range(len(self.socatServers)):
			socatLaunch = f"{self.noaaportPath}/{self.socatLaunchers[ socatIdx ]} > {self.logFile}  &"
			print(f'About to execute: {socatLaunch}')

			os.system( socatLaunch )
	
		# 2. Start the blender
		# (Use subprogram to control the status instead of os.system)
		print(f'About to execute blender: {self.blenderLauncher}')
		os.system(self.blenderLauncher )

class Blender(TestBlender):

	socatServers={
		"socat_1": 				"localhost:9127",
		"socat_2": 				"localhost:9128"
	    	# 	,
	    	#"socat_3": 				"localhost:9129",
	    	#"socat_4": 				"localhost:9130",
	    	#"socat_5": 				"localhost:9131",
	    	#"socat_6": 				"localhost:9132",
	    	#"socat_7": 				"localhost:9133",
	    	#"socat_8": 				"localhost:9134",
	    	#"socat_9": 				"localhost:9135",
	    	#"socat_10":				"localhost:9136"        	    	
	}

	def __init__(self, testValue):
    	#TestBlender.__init__(self, testValue)
		TestBlender.__init__(self, True)

def main():

	system('clear')
	
	testing = False
	if testing == True:
		# Testing
		testBlenderInst = TestBlender(True)
		testBlenderInst.execute()

	else:

		# Production
		blenderInst 	= Blender(False)	
		blenderInst.execute()


if __name__ == '__main__':

	main()
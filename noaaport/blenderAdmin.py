#!/usr/bin/env python3

###############################################################################
#
#
# Description: This Python script provides a command line interface to LDM
#  programs. It is invoked as an executabe script.
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
# Files:
#
#  $LDMHOME/ldmd.pid         file containing process group ID
#  $LDMHOME/.ldmadmin.lck    lock file for operations that modify the state of
#                            the LDM system
#  $LDMHOME/.[0-9a-f]*.info  product-information of the last, successfuly-
#                            received data-product
#
###############################################################################

import 	os
from 	os       import environ, system
import 	sys
import 	readline

from signal import signal, SIGINT
from time 	import sleep


class TestBlender:

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

	# blender
	timeOut 		= "-t 0.01"
	logFile 		= "-l /tmp/blender.log"
	blenderArgs 	= f" {timeOut} {logFile}"
	#blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_2.log localhost:9127 localhost:9128"
	blenderLauncher	= ""

	def __init__(self):	
		
		ldmHome     = environ.get("LDMHOME", "/home/miles/projects/ldm")
		self.noaaportPath	= f"{ldmHome}/src/noaaport"
		
		socatList 			= ""

		#print(f"\n--> Build testBlender commands...\n")
		for socat, hostId in self.socatServers.items():
			#print(f"\t{socat} \t{hostId}")
			port = hostId.split(":")[1]			

			self.socatLaunchers.append( f"testBlender {self.socatArgs} {port}"   )
			socatList += hostId + " "

		#print(f"\n--> Build the blender command...\n")
		self.blenderLauncher = f"{self.noaaportPath}/blender {self.blenderArgs} {socatList} &"


	def execute(self):

		for socatIdx in range(len(self.socatServers)):
			socatLaunch = f"{self.noaaportPath}/{self.socatLaunchers[ socatIdx ]} > /tmp/testBlender.log &"
			print(f'About to execute: {socatLaunch}')

			os.system( socatLaunch )
	
		# Use subprogram to control the status instead of os.system
		print(f'About to execute blender: {self.blenderLauncher}')
		os.system(self.blenderLauncher )

class Blender:

	socatServers={
	    	"socat_1": 				"localhost:9127",
	    	"socat_2": 				"localhost:9128",
	    	"socat_3": 				"localhost:9129",
	    	"socat_4": 				"localhost:9130",
	    	"socat_5": 				"localhost:9131",
	    	"socat_6": 				"localhost:9132",
	    	"socat_7": 				"localhost:9133",
	    	"socat_8": 				"localhost:9134",
	    	"socat_9": 				"localhost:9135",
	    	"socat_10":				"localhost:9136"        
    }


def main():

	system('clear')
	

	testBlenderInst 	= TestBlender()	# instance of 'this'
	os.chdir(testBlenderInst.noaaportPath)	
	print("blender's home directory: ", os.getcwd())

	print("\n---------------- Execute ----------\n testBlender 5   6  2000000   1       9127 \n")
	#eval("testBlender") # 5   6  2000000   1       9127")
	print("\n---------------- Execute ----------\n")
	testBlenderInst.execute()



if __name__ == '__main__':

	main()
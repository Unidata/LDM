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
import  argparse


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

	# testBlender args
	nbrFrames	= 5
	nbrRuns		= 6
	runAndWait	= 2000000
	snoozeTime	= 1
	#5   6  2000000   1       9127"
	testBlenderArgs	= f" {nbrFrames} {nbrRuns} {runAndWait} {snoozeTime} " 

	# blender  args
	#blenderLaunch 	= "blender -t 0.01  -l /tmp/blender_out.log 9127 localhost, chico"
	timeOut 		= "0.01"
	

	def __init__(self):	
		
		ldmHome     		= environ.get("LDMHOME", "/home/miles/projects/ldm")	# remove 'miles' path
		self.noaaportPath	= f"{ldmHome}/src/noaaport"

		# Check CLI options:
		self.cliParserInit = argparse.ArgumentParser(
			prog='blenderAdmin',
			description='''This file is the blenderAdmin script that 
						   is used to launch the blender that requests SBN data from fanout servers.
						''',
			usage='''\n\n\t%(prog)s [-l logFile][-x][-r][--test] <port> <hostId> ...  \n
			port	Unique port number for all hosts
			hostIds One or more hosts specified as hostnames or hosts
			-l 	Log file (default: /tmp/blender.log)
			-r 	Redirect blender standard output to /dev/null
			--test 	Test mode. Specify: 
				- 'localhost' as a <hostIds>
				-  any port number as <port>
			-x 	Debug mode
			''',
			epilog='''
				Thank you for using %(prog)s...
					'''
			)



		# Check if program(s) are running. If so, kill them
		self.checkRunning()
		system('clear')

	def prepareCmd(self, cliArg):

		blenderArgs 	= f" -t {self.timeOut} "
		if cliArg["debugMode"] == True:
			blenderArgs 	+= " -x "

		if cliArg["logFile"] != None:
			blenderArgs 	+= f' -l {cliArg["logFile"]}'

		for hostId in cliArg["hostIds"]:
			blenderArgs += f" {hostId}:{cliArg['singlePort'][0]} "

		# This should be last in the blenderArgs variable
		if cliArg["redirectOutput"] == True:
			blenderArgs 	+= " >/dev/null"

		
		# Build the blender command ------------------------------------
		return f"{self.noaaportPath}/blender {blenderArgs} &"


	def execute(self, cliArg):
		
		# Launch testBlender locally:
		if cliArg["testMode"] == True:
			self.testBlenderArgs += f' { cliArg["singlePort"][0] }'
			# 5   6  2000000   1       9127
			cmd = f"{self.noaaportPath}/testBlender {self.testBlenderArgs} &"
			print(cmd)
			system( cmd )
		

		# 2. Start the blender
		cmd = self.prepareCmd(cliArg)
		print(cmd)
		system( cmd )


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
			

	def cliParser(self):

		self.cliParserInit.add_argument('-x', dest='debugMode', action='store_true', help='', required=False)
		self.cliParserInit.add_argument('-r', dest='redirectOutput', action='store_true', help='', required=False)
		self.cliParserInit.add_argument('-l', dest='logFile', action="store", help='', required=False)
		self.cliParserInit.add_argument('singlePort', nargs=1, type=int, default='9127', help='<Required>', metavar='port')
		self.cliParserInit.add_argument('hostIds', nargs='+', type=str, action="store", help='<Required>', metavar='hostId')
		self.cliParserInit.add_argument('--test', dest='testMode', action='store_true', help='', required=False)
		args, other = self.cliParserInit.parse_known_args()
		
		return vars(args)     # vars(): converts namespace to dict
		


	def errmsg(self, msg):
		print(f"\n\tNote: {msg}")


def main():

	system('clear')
	
	# Production
	blenderInst = Blender()	
	cliDico = blenderInst.cliParser()
	blenderInst.execute(cliDico)


if __name__ == '__main__':

	main()
#!/usr/bin/env python3

###############################################################################
#
#
# Description: This Python script provides a command line interface to LDM
#  programs. It is invoked as an executabe script.
#
#
#   @file:  ldmadmin
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


from os import system
import sys
import readline

from signal import signal, SIGINT
from sys 	import exit
from time 	import sleep

import parseCLI 		as parseCli
import parseRegistry 	as regHdler
import environHandler	as envHdler
import ldmSubs			as sub
import ldmUtils			as util

EXIT_MESSAGE='\n\tThank you for using ldmadmin...\n\n'


def bye(signal_received, frame):
	# Handle any cleanup here
	exitMessage("")


def exitMessage(msg):
	print(f"\n{msg} \n{EXIT_MESSAGE}")
	exit(0)


class LDMCommandsHandler:
	def __init__(self, cmdsDico):
		self.cmdsDico	= cmdsDico
		self.ldmShortCmdsList	= []
		for cmd, optionList in cmdsDico.items():
			self.ldmShortCmdsList.append(cmd)
	        
	def complete(self,userInput,state):
		results =  [cmd + " " for cmd in self.ldmShortCmdsList if cmd.startswith(userInput)] + [None]
		return results[state]

	def isValidCmd(self, cmd):		
		return cmd in self.ldmShortCmdsList

	def returnCmdCortege(self, cmd):
		return self.cmdsDico[cmd][0]

	def displayRegistryAndEnv(self):

		envt = envHdler.LDMenvironmentHandler()
		regH = regHdler.RegistryParser()
    
		envt.prettyPrintEnvVars()
		regH.prettyPrintRegistry()


	def cmdDispatcher(self, cmd, reg, env):

		ldmCommandsDict={
	    	"start": 				"sub.start_ldm(reg, env)",
	    	"stop":					"sub.stop_ldm(reg, env)",
	        "restart": 				"sub.restart(reg, env)",
	        "mkqueue":				"sub.make_pq(reg, env)",
	        "delqueue":				"sub.del_pq(reg, env)",
	        "mksurfqueue":			"sub.make_surf_q(reg, env)",
	        "delsurfqueue":			"sub.del_surf_pq(reg, env)",
	        "newlog":				"sub.newLog(reg, env)",
	        "scour":				"sub.scour(reg)",
	        "isrunning":			"sub.isRunning(reg, env, pingFlag)",
	        "checkinsertion": 		"sub.check_insertion(reg)",
	        "vetqueuesize":			"sub.vetQueueSize(reg, env)",
	        "check":				"sub.checkLdm(reg, env)",
	        "watch":				"sub.watch(reg, env)",
	        "pqactcheck":			"sub.pqactcheck(reg, env)",
	        "pqactHUP":				"util.pqactHUP(env)",
	        "queuecheck":			"sub.queueCheck(reg, env)",	        
	        "config":				"sub.ldmConfig(reg, env)",
	        "log":					"sub.pageLog(reg)",
	        "tail":					"sub.tailLog(reg)",
	        "clean":				"sub.clean(reg, env)", 
	        "checktime":			"sub.checkTime(reg)",
	        "printmetrics":			"util.printMetrics(reg)",
	        "addmetrics":			"util.addMetrics(reg)",
	        "plotmetrics":			"util.plotMetrics(reg, env)",
	        "newmetrics":			"util.newMetrics(reg)",
	        "updategempaktables": 	"sub.updateGempakTables()"
    	}

		pingFlag = True

		eval(ldmCommandsDict[cmd])



	# cortege not needed here. move its values (begin, end, etc.) to envVar
	def execute(self, cmd, reg, envVar, envt):

		status 				= 0
		pqact_conf_option 	= envVar['pqact_conf_option']

		toLockOrNot 		= parseCli.CLIParser().isLockingRequired(cmd)
		cmdToExecute = f"{cmd} " # {cortege}"
		if toLockOrNot == True:
			if envt.getLock() == -1:
				print(f"Could not get lock for '{cmdToExecute}' to execute properly!")
				status = -1
				return status

			#print(f"\nExecuting in locked mode : \t{cmdToExecute}\n")
			#cmdToExecute = f"{cmdToExecute}, lock=True, pqact_conf_option={pqact_conf_option}"

			self.cmdDispatcher(cmd, reg, envVar)
			envt.releaseLock()

		else:
			#print(f"\nExecuting in NON locked mode : \n\n\t{cmdToExecute}\n")
			#cmdToExecute = f"{cmdToExecute}, lock=False, pqact_conf_option={pqact_conf_option}"
			
			self.cmdDispatcher(cmd, reg, envVar)

		return status


def main():
	signal(SIGINT, bye)
	system('clear')
	debug = True #False

	
	regParser 	= regHdler.RegistryParser()
	envt		= envHdler.LDMenvironmentHandler()
	# Registry dict:
	regDico 	= regParser.getRegistryEntries()


	# tab completion:
	readline.parse_and_bind("tab: complete")
	cliInst 		= parseCli.CLIParser()				# instance of CLIParser
	cmdsDico 		= cliInst.getFullCommandsDict()
		
	LDMcommands 	= LDMCommandsHandler( cmdsDico )	# instance of 'this'
	
	

	if debug:
		LDMcommands.displayRegistryAndEnv()

	readline.set_completer(LDMcommands.complete)



	nbArguments=len(sys.argv)
	if nbArguments == 1 or \
		nbArguments == 2 and (sys.argv[1] == "-h" or sys.argv[1] == "--help"):

# Interactive mode
		cliInst.usage()
		print(f"\n\tInteractive mode (type 'quit' to exit). Not implemented yet.\n")
	

	else:

# Non-interactive mode (CLI mode)
		cmd=sys.argv[1]

		# Ensure that the registry is available because a locked registry will cause
		# this script to hang. (Is this valid for a Python script??)
		if sub.resetRegistry():
		    	exitMessage("Registry unavailable! Bailing out!")

		if not LDMcommands.isValidCmd(cmd):
			print(f"\n\tInvalid ldmadmin command: {cmd}\n")
			sleep(3)
			cliInst.usage()
			exitMessage("")

		# Here, cmd is a valid ldmadmin command:
		if cmd == "usage": 
			cliInst.usage()

		
		# if  nbArguments == 2: # command w/o options
		# 	status 			= LDMcommands.execute(cmd, envt)
		# else:	


		cliDico 	= cliInst.cliParserAddArguments(cmd)
		cliCortege 	= cliInst.buildCLIcortege(cmd, cliDico) # cortege is not needed, the call is.
		#print(f"\n\nCLI dict: {cliDico}\n")
		
		# Copy the cli arguments to envVar to make them available at execution time
		envVar  	= envt.getEnvVarsDict()
		util.copyCliArgsToEnvVariable(envVar, cliDico)

		#print("\n---------------- envVar ----------\n")
		#print(envVar)
		
		LDMcommands.execute(cmd, regDico, envVar, envt)

		# last line:
		exitMessage("")

# Interactive mode ###################################################################

	# if None:
	# 	cmd = input('ldmadmin> ')
	# 	while not cmd == "quit":
			
	# 		cmd=cmd.strip()
	# 		if cmd == "usage": 
	# 			cliInst.usage()


	# 		if cmd == "quit":
	# 			exitMessage("")

	# 		if not LDMcommands.isValidCmd(cmd):
	# 			print(f"Invalid ldmadmin command: {cmd}\n")
	# 			cmd = input('ldmadmin> ')
	# 			continue

	# 		# Here, cmd is a valid ldmadmin command:
	# 		print(f"{cmd} ---> {LDMcommands.returnCmdCortege(cmd)}")

	# 		# namespace = cliInst.cliParserAddArguments(cmd)
	# 		# #print(f"\n\tnamespace: {namespace}\n")

	# 		# cliCortege = cliInst.buildCLIcommand(cmd, namespace)
	# 		# status = LDMcommands.execute(cmd, cliCortege, envt)



	# 		# last line:
	# 		cmd = input('ldmadmin> ')


	# exitMessage("")

if __name__ == '__main__':

	main()


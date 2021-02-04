#!/usr/bin/env python3

 #
 #
 # This file helps testing the C-based scour program - Cscour(1) - of the Unidata LDM package.
 #
 #  @file:  testCscour.py
 # @author: Mustapha Iles
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



import os
from os import path
from pathlib import Path
import time
import subprocess
from subprocess import PIPE
import sys
import errno
import shutil

ASSERT_FILE_DELETED	="Expect file to be deleted"
ASSERT_DIR_DELETED	="Expect directory to be deleted"
ASSERT_DIR_LINK_NOT_DELETED	="Expect directory and symlink NOT be deleted"
ASSERT_FILE_LINK_DELETED	="Expect file and its symlink be deleted"
ASSERT_SUCCESS		="SUCCESS"
ASSERT_FAIL			="FAIL"

class CommonFileSystem:

	dirList=[
			"~/vesuvius",
			"~/etna",
			"~/etna/alt_dir"
		]		

	fileList=["~/vesuvius/.scour$*.foo", 
				"~/vesuvius/precipitation.foo",
				"~/vesuvius/precipitation.txt",
				"~/etna/precipitation.txt",
				"~/etna/alt_dir/precipitation.txt",
				"~/etna/vesuvius.foo",
				"~/etna/.scour$*.foo"
				]

	def __init__(self, filename, timestamp):
		self.filename = filename
		self.timestamp = timestamp

	def __str__(self):
		return f"{self.filename}  {self.timestamp}"

	__repr__ = __str__

	def __eq__(self, other):
		if self.filename == other.filename:
			return true
		return false

	def changeIt(self, timestamp):
		self.timestamp = timestamp
		return self;

	def createFiles(fileList):

		for file in fileList:
			expandedFile =  os.path.expanduser(file)
			if not os.path.exists(expandedFile):
				print(f"\tCreating file: {expandedFile}")
				f = open(expandedFile, "w+")
				f.close()

		# Initialize timestamp to zero
		fileDict={}.fromkeys(fileList,0)
		return fileDict

	def createDirectories():

		for directory in CommonFileSystem.dirList:
			
			expandedDir =  os.path.expanduser(directory)
			if os.path.exists(expandedDir):
				print(f"\tDirectory {expandedDir} exists.")
				continue

			try:
				print(f"\tCreating directory: {expandedDir}")
				os.mkdir(expandedDir)
			except OSError as e:
				print(e)
			

	def removeDirectories():

		for directory in CommonFileSystem.dirList:
			
			expandedDir =  os.path.expanduser(os.path.expandvars(directory))
			try:
				if os.path.exists(expandedDir):
					print(f"\tRemoving directory: {expandedDir}")
					shutil.rmtree(expandedDir)		
			except OSError as e:
				print(e)
			

	def getMTime(aPath):
		mtime = Path(aPath).stat().st_mtime

		print(f"\tCurrent mtime: \t{CommonFileSystem.convertEpochToHumanDate(mtime)} - ({int(mtime)}) for {aPath}")
		
		return int(mtime) 


	def changeMTime(aPath, secondsIncrement):
		mtime = Path(aPath).stat().st_mtime

		#with open(precipitation, 'wt') as f: pass
		result = os.stat(Path(aPath))

		# change mtime:
		currentTimeEpoch = int(time.time())
		negativeNewTimeEpoch = currentTimeEpoch - secondsIncrement
		os.utime(aPath, (negativeNewTimeEpoch, negativeNewTimeEpoch))
#		os.utime(aPath, (result.st_atime + secondsIncrement, result.st_mtime + secondsIncrement))
		result = os.stat(aPath)
		print(f"\tNew mtime: \t{CommonFileSystem.convertEpochToHumanDate(int(result.st_mtime))} - ({int(result.st_mtime)}) for {aPath}")
		


		return int(result.st_mtime)
		

	def convertEpochToHumanDate(epochTime):

		
		mtime = time.gmtime(epochTime)
		#time.struct_time(tm_year=2012, tm_mon=8, tm_mday=28, tm_hour=0, tm_min=45, tm_sec=17, tm_wday=1, tm_yday=241, tm_isdst=0)  

		humanTime = time.strftime('%m/%d/%Y %H:%M:%S',  time.gmtime(epochTime))
		# '08/28/2012 00:45:17'

		return humanTime



	def executeCscour(deleteFlag, ingestFile):
		if deleteFlag == "-d":
			process = subprocess.Popen(
			    ["./Cscour", "-v", deleteFlag,  ingestFile], stderr=PIPE, stdout=PIPE)
		else:
			process = subprocess.run(
			    ["./Cscour", "-v",  ingestFile], stderr=PIPE, stdout=PIPE)
		out, err = process.communicate()

		f = open("/tmp/scour_log.txt", "w+")
		f.write(CommonFileSystem.unwrapIt(out))
		f.close()

		print(CommonFileSystem.unwrapIt(out))
		

	def unwrapIt(text):
		s=""
		for i in text:
			#if chr(i) == '\n': continue
			
			s+= chr(i)
		
		return s
		


# Scenario: 
#	- Create a directory tree
#	- Create a symlink to a file eligible for deletion
#	- Create  a directory symlink
# 	Expected:
#	- file gets deleted and its symlink too
#	- directory becomes empty (all its scoured files are deleted OR some files remain)
#   - directory (and its symlink) is not deleted.

class SymlinkDeletion:	

	fileList=[	"~/vesuvius/.scour$*.txt", 
				"~/vesuvius/precipitation.foo",
				"~/vesuvius/precipitation.txt",
				"~/etna/precipitation.txt",
				"~/etna/alt_dir/precipitation.txt",
				"~/etna/vesuvius.foo",
				"~/etna/.scour$*.foo"
	]
	def __init__(self):
		
		self.ingestFile 	= "/tmp/scourTest.conf"
		#self.entries 		= "~/vesuvius		2	*.txt\n~/etna		7-1130	*.foo"
		self.entries 		= "~/vesuvius		2	*.txt"
		self.daysOldInSecs 	= 172800 			# seconds: 2 * 24 * 3600 s

		# a dict has .items(). .keys(), .values()
		self.fileToSymlinkDict={
				"~/etna/precipitation.txt": "~/vesuvius/sl_etna_file",
				"~/etna/alt_dir": "~/vesuvius/sl_etna_alt_dir"
			}
		# Create the scour config file with entries 
		# for Cscour to use as CLI argument
		f = open(self.ingestFile, "w+")
		f.write(self.entries)
		f.close()

	def __str__(self):
		return f"{self.filename}  {self.timestamp}"

	def __eq__(self, other):
		if self.filename == other.filename:
			return true
		return false

	def createSymlinks(self):

		print(f"\nScour config file entrie(s): \n{self.entries}")


		print(f"\nCreate symlink(s): ")

		for kTarget, vLink  in self.fileToSymlinkDict.items():
			expandedTarget =  os.path.expanduser(kTarget)
			expandedLink =  os.path.expanduser(vLink)

			print(f"\t{expandedLink} \t--> {expandedTarget}")
			if not os.path.exists(expandedLink):
				os.symlink(expandedTarget, expandedLink)
			
		print("\n")


	def markEligibleForDeletion(self, daysOldInSecs):

		# Set mtime to appropriate value to have file deleted
		# for kTarget, vLink  in self.fileToSymlinkDict.items():
		# 	expandedTarget =  os.path.expanduser(kTarget)
		# 	expandedLink =  os.path.expanduser(vLink)
		# 	CommonFileSystem.changeMTime(expandedTarget, daysOldInSecs)

		# make .scour$*.txt NEWER than precipitation.txt
		# Expect precipitation.txt to be deleted.
		
	#	scourFile=os.path.expanduser("~/vesuvius/.scour$*.txt")
	#	CommonFileSystem.changeMTime(scourFile, daysOldInSecs)


		# make precipitation.txt OLDER than 2days +50secs
		# Expect precipitation.txt to be deleted.
		aFile=os.path.expanduser("~/vesuvius/precipitation.txt")
		CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)


		aFile=os.path.expanduser("~/etna/alt_dir/precipitation.txt")
		CommonFileSystem.changeMTime(aFile, daysOldInSecs +50)


		# make precipitation.txt OLDER than 2days +50secs
		# Expect precipitation.txt to be deleted.
		aFile=os.path.expanduser("~/etna/precipitation.txt")
		CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)



	def createFiles(self):
		print("\nCreate files:")
		CommonFileSystem.createFiles(self.fileList)


	def runScenario(self, deleteFlag):
		self.createFiles()
		self.createSymlinks()
		self.markEligibleForDeletion(self.daysOldInSecs)
		
		print(f"\n\tExecuting Scour...\n")
		CommonFileSystem.executeCscour(deleteFlag, self.ingestFile)
		
		print(f"\n\tRunning assertions...\n")
		self.assertSymlink()


	def assertSymlink(self):
		
		# Files:
		print(f"\nFILES: --------------------------------------")
		aPath=os.path.expanduser("~/etna/alt_dir/precipitation.txt")
		if not os.path.exists(aPath): 
			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
		else:
			print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")

		aPath=os.path.expanduser("~/vesuvius/precipitation.txt")
		if not os.path.exists(aPath): 
			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
		else:
			print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")


		aPath=os.path.expanduser("~/etna/precipitation.txt")
		if not os.path.exists(aPath): 
			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
		else:
			print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")

		# Directories
		print(f"\nDIRECTORIES: --------------------------------------")


		# Symlinks
		
				# "~/etna/precipitation.txt": "~/vesuvius/sl_etna_file",
				# "~/etna/alt_dir": "~/vesuvius/sl_etna_alt_dir"
		print(f"\nSYMLINKs: --------------------------------------")
		kTarget	= "~/etna/precipitation.txt"
		vLink 	= "~/vesuvius/sl_etna_file"
		expandedTarget =  os.path.expanduser(kTarget)
		expandedLink =  os.path.expanduser(vLink)


		print(f"\t{expandedLink} --> {expandedTarget}")
		if 	not os.path.exists(expandedTarget)	\
			and not os.path.islink(expandedLink):
			
			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_LINK_DELETED}: {expandedLink}")			
			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {expandedTarget}")
		else:
			if os.path.exists(expandedTarget)	\
				or os.path.islink(expandedLink):
					
				print(f"\n\t{ASSERT_FAIL}: \tUn-Expected SymLink behavior! ({expandedTarget} - {expandedLink})  ")
				


		# "~/etna/alt_dir": "~/vesuvius/sl_etna_alt_dir"
		kTarget	= "~/etna/alt_dir"
		vLink 	= "~/vesuvius/sl_etna_alt_dir"
		expandedTarget =  os.path.expanduser(kTarget)
		expandedLink =  os.path.expanduser(vLink)

		print(f"\t{expandedLink} --> {expandedTarget}")
		# Do not delete directory and its symlink
		if 	os.path.exists(expandedTarget) 		\
			and os.path.isdir(expandedTarget) 	\
			and os.path.exists(expandedLink) 	\
			and os.path.islink(expandedLink): 

			print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_LINK_NOT_DELETED}: {expandedTarget}")				
	




# Scenario: 
#	- Create a directory tree with files that all eligible for deletion (no symlink)
#	- Expected: all tree gets deleted

class EmptyDirectoriesDeletion:

	fileList=["~/vesuvius/.scour$*.foo", 
				"~/vesuvius/precipitation.foo",
				"~/vesuvius/precipitation.txt",
				"~/etna/precipitation.txt",
				"~/etna/alt_dir/precipitation.txt",
				"~/etna/vesuvius.foo",
				"~/etna/.scour$*.foo"
	]

	dirList=[
			"~/vesuvius",
			"~/etna",
			"~/etna/alt_dir"
	]
	
	def __init__(self):
		
		self.ingestFile = "/tmp/scourTest.conf"
		#self.entries 	= "~/vesuvius		2	*.txt\n~/etna		7-1130	*.foo"
		self.entries 	= "~/vesuvius		2	*.txt"
		# a dict has .items(). .keys(), .values()
		
		# Create the scour config file with entries 
		# for Cscour to use as CLI argument
		f = open(self.ingestFile, "w+")
		f.write(self.entries)
		f.close()

	def __str__(self):
		return f"{self.filename}  {self.timestamp}"

	def __eq__(self, other):
		if self.filename == other.filename:
			return true
		return false


	def runScenario(self, deleteFlag):

		self.createFiles()
		self.markEligibleForDeletion("YES")
		
		print(f"\n\tExecuting Scour...\n")
		CommonFileSystem.executeCscour(deleteFlag, self.ingestFile)
		
		print(f"\n\tRunning assertions...\n")
		self.assertSymlink()



	def createFiles(self):

		print("\nCreate files:")
		CommonFileSystem.createFiles(self.fileList)


	def assertSymlink(self):
		pass

def main():


		print("\nTear down directories:")
		CommonFileSystem.removeDirectories()

		print("\nCreate directories:")
		CommonFileSystem.createDirectories()

		#-----------------------------------------------#

		print("\n\t========= Symlink Deletion scenario ================\n")
		sl = SymlinkDeletion()
		sl.runScenario("-d")
		

		print("\n\t========= EmptyDirectories Deletion scenario ================\n")
		#ed = EmptyDirectoriesDeletion()
		#ed.runScenario("-d")
		


if __name__ == '__main__':
		main()
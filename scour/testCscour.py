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

import subprocess
from subprocess import PIPE
import sys
import errno
import shutil


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

		print(f"\t{aPath} - mtime: \t{int(mtime)}")
		return int(mtime) 

	def changeMTime(precipitation, secondsIncrement):
		mtime = Path(precipitation).stat().st_mtime

		#with open(precipitation, 'wt') as f: pass
		result = os.stat(Path(precipitation))

		# change mtime:
		os.utime(precipitation, (result.st_atime, result.st_mtime + secondsIncrement))
		result = os.stat(precipitation)
		print(f"{precipitation} - Changed mtime:\t{int(result.st_mtime)}")

		return int(result.st_mtime)
		
	def executeCscour(deleteFlag, ingestFile):
		if deleteFlag == "":
			result = subprocess.run(
			    ["./Cscour", "-v",  ingestFile], stderr=PIPE, stdout=PIPE)
		else:
			result = subprocess.run(
			    ["./Cscour", "-v", deleteFlag,  ingestFile], stderr=PIPE, stdout=PIPE)
			
		print("\nstdout:\n", result.stdout)
		print("\nstderr:\n", result.stderr)


# Scenario: 
#	- Create a directory tree
#	- Create a symlink to a file eligible for deletion
#	- Create  a directory symlink
# 	Expected:
#	- file gets deleted and its symlink too
#	- directory becomes empty (all its scoured files are deleted OR some files remain)
#   - directory (and its symlink) is not deleted.

class SymlinkDeletion:	

	fileList=[	"~/vesuvius/.scour$*.foo", 
				"~/vesuvius/precipitation.foo",
				"~/vesuvius/precipitation.txt",
				"~/etna/precipitation.txt",
				"~/etna/alt_dir/precipitation.txt",
				"~/etna/vesuvius.foo",
				"~/etna/.scour$*.foo"
	]
	def __init__(self):
		
		self.ingestFile = "/tmp/scourTest.conf"
		#self.entries 	= "~/vesuvius		2	*.txt\n~/etna		7-1130	*.foo"
		self.entries 	= "~/vesuvius		2	*.txt"
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

		print(f"Scour entries: \n{self.entries}\n")

		for kTarget, vLink  in self.fileToSymlinkDict.items():
			expandedTarget =  os.path.expanduser(kTarget)
			expandedLink =  os.path.expanduser(vLink)

			print(f"\t{expandedLink} --> {expandedTarget}")
			if not os.path.exists(expandedLink):
				os.symlink(expandedTarget, expandedLink)
			
			#if os.path.isdir(expandedTarget): 
			#	continue
			
			print("\n\n")
			CommonFileSystem.getMTime(expandedTarget)
			CommonFileSystem.changeMTime(expandedTarget, 100)
			CommonFileSystem.getMTime(expandedTarget)
			print("\n\n")

	def markEligibleForDeletion(self, YES_NO):
		# Set mtime to appropriate value to have file deleted
		pass


	def createFiles(self):
		print("\nCreate files:")
		CommonFileSystem.createFiles(self.fileList)


	def runScenario(self, deleteFlag):
		self.createFiles()
		self.createSymlinks()
		self.markEligibleForDeletion("YES")
		
		print(f"\n\tExecuting Scour...\n")
		CommonFileSystem.executeCscour(deleteFlag, self.ingestFile)
		
		print(f"\n\tRunning assertions...\n")
		self.assertSymlink()


	def assertSymlink(self):
		
		self.fileToSymlinkDict={
				"~/etna/precipitation.txt": "~/vesuvius/sl_etna_file",
				"~/etna/alt_dir": "~/vesuvius/sl_etna_alt_dir"
			}



		for kTarget, vLink  in self.fileToSymlinkDict.items():
			expandedTarget =  os.path.expanduser(kTarget)
			expandedLink =  os.path.expanduser(vLink)

			print(f"\t{expandedLink} --> {expandedTarget}")
			if os.path.exists(expandedTarget) and os.path.isdir(expandedTarget): 
				print(f"{expandedTarget}: \tExpected directory NOT DELETED. \tOK")
				continue
			#  /home/miles/vesuvius/sl_etna_alt_dir --> /home/miles/etna/alt_dir
			if not os.path.exists(expandedTarget):
				print(f"{expandedTarget}: \tExpected file DELETED. \tOK")
				continue
			else:
				print(f"{expandedTarget}: \tExpected file DELETED. \tNot OK")
				
	



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


	def markEligibleForDeletion(self, YES_NO):
		# Set mtime to appropriate value to have file deleted

		for directory in CommonFileSystem.dirList:
			
			expandedDir =  os.path.expanduser(directory)
			if os.path.exists(expandedDir):
				print(f"\t{expandedDir}")


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
		ed = EmptyDirectoriesDeletion()
		ed.runScenario("-d")
		


if __name__ == '__main__':
		main()
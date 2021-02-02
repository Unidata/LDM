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

import errno
import shutil


class CommonFileSystem:
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

	def createFiles():
		fileList=["~/titi/.scour$*.foo", 
				"~/titi/aFileToo.foo",
				"~/titi/aFile.txt",
				"~/toto/tata.txt",
				"~/toto/titi.foo",
				"~/toto/.scour$*.foo"
				]
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
		dirList=[
			"~/titi",
			"~/toto",
			"~/toto/tut_dir"
		]
		for directory in dirList:
			
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
		dirList=[
			"~/titi",
			"~/toto",
			"~/toto/tut_dir"
		]

		for directory in dirList:
			
			expandedDir =  os.path.expanduser(directory)
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

	def changeMTime(aFile, secondsIncrement):
		mtime = Path(aFile).stat().st_mtime

		with open(aFile, 'wt') as f: pass
		result = os.stat(aFile)

		# change mtime:
		os.utime(aFile, (result.st_atime, result.st_mtime + secondsIncrement))
		result = os.stat(aFile)
		print(f"{aFile} - Changed mtime:\t{int(result.st_mtime)}")

		return int(result.st_mtime)
		

#Scenario: symlink to a file and to a directory
#	- file is eligible for deletion gets delete and its symlink too
#	- directory either gets empty (all its scoured files are eligible for deletion)
#     or some files remain. In both cases the directory does not get deleted.

class SymlinkDeletion:	

	def __init__(self):
		
		self.ingestFile = "/tmp/scourTest.conf"
		self.entries 	= "~/titi		2	*.txt\n~/toto		7-1130	*.foo"

		f = open(self.ingestFile, "w+")
		f.write(self.entries)
		f.close()

	def __str__(self):
		return f"{self.filename}  {self.timestamp}"

	__repr__ = __str__

	def __eq__(self, other):
		if self.filename == other.filename:
			return true
		return false

	def createSymlinks(self):

		print(f"Scour entries: \n{self.entries}\n")

		# a dict has .items(). .keys(), .values()
		fileToSymlinkDict={
				"~/toto/tata.txt": "~/titi/sl_toto_file",
				"~/toto/tut_dir": "~/titi/sl_toto_tut_dir"
			}

		for kTarget, vLink  in fileToSymlinkDict.items():
			expandedTarget =  os.path.expanduser(kTarget)
			expandedLink =  os.path.expanduser(vLink)

			print(f"\t{expandedLink} --> {expandedTarget}")
			if not os.path.exists(expandedLink):
				os.symlink(expandedTarget, expandedLink)
			
			if os.path.isdir(expandedTarget): 
				continue
			
			print("\n\n")
			CommonFileSystem.changeMTime(expandedTarget, 100)
			CommonFileSystem.getMTime(expandedTarget)
			print("\n\n")

def main():


		print("\nCleanup directories:")
		CommonFileSystem.removeDirectories()

		print("\nList of directories:")
		CommonFileSystem.createDirectories()

		print("\nList of files:")
		CommonFileSystem.createFiles()

		#-----------------------------------------------#

		print("\n\t\tSymlinkDeletion scenario\n")
		s = SymlinkDeletion()

		SymlinkDeletion.createSymlinks(s)

		

if __name__ == '__main__':
		main()
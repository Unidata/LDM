#!/usr/bin/env python3

 #
 #
 # This file helps testing the C-based scour program - scour(1) - of 
 # the Unidata LDM package.
 #
 # @file:  testCscour.py
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
from os import system
from os import path
from pathlib import Path
import time
import subprocess
from subprocess import PIPE
import sys
import errno
import shutil
import argparse

ASSERT_FILE_DELETED         ="Expect file to be deleted"
ASSERT_FILE_LINK_DELETED    ="Expect file and its symlink be deleted"
ASSERT_DIR_DELETED          ="Expect directory to be deleted"
ASSERT_NOT_DIR              ="Expect 'not-a-directory' to be skipped"
ASSERT_DIR_LINK_NOT_DELETED ="Expect directory and symlink NOT be deleted"
ASSERT_EXCLUDED_DIR_NOT_DELETED ="Expect directory to be excluded"
ASSERT_SUCCESS              ="SUCCESS"
ASSERT_FAIL                 ="FAIL"

class CommonFileSystem:

    dirList=[
            "/tmp/vesuvius",
            "/tmp/etna",
            "/tmp/etna/alt_dir",
            "/tmp/etna/exclude_me"
        ]        

    fileList=["/tmp/vesuvius/.scour$*.foo", 
                "/tmp/vesuvius/precipitation.foo",
                "/tmp/vesuvius/precipitation.txt",
                "/tmp/etna/precipitation.txt",
                "/tmp/etna/alt_dir/precipitation.txt",
                "/tmp/etna/vesuvius.foo",
                "/tmp/etna/.scour$*.foo",
                "/tmp/excludes.conf"
                ]

    def __init__(self, filename, timestamp):
        self.filename  = filename
        self.timestamp = timestamp

        self.excludes  = "/tmp/excludes.conf"
        self.entries   = "/tmp/etna/exclude_me"
        
        # Create the excludes file with one entry 
        # for scour to use as CLI argument for -e option
        f = open(self.excludes, "w+")
        f.write(self.entries)
        f.close()

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

    def createFiles(fileList, debug):

        for file in fileList:
            expandedFile =  os.path.expanduser(file)
            if not os.path.exists(expandedFile):
                debug and print(f"\tCreating file: {expandedFile}")
                f = open(expandedFile, "w+")
                f.close()

        # Initialize timestamp to zero
        fileDict={}.fromkeys(fileList,0)
        return fileDict

    def createDirectories(debug):

        for directory in CommonFileSystem.dirList:
            
            expandedDir =  os.path.expanduser(directory)
            if os.path.exists(expandedDir):
                debug and print(f"\tDirectory {expandedDir} exists.")
                continue

            try:
                debug and print(f"\tCreating directory: {expandedDir}")
                os.mkdir(expandedDir)
            except OSError as e:
                print(e)
            

    def removeDirectories(debug):

        for directory in CommonFileSystem.dirList:
            
            expandedDir =  os.path.expanduser(os.path.expandvars(directory))
            try:
                if os.path.exists(expandedDir):
                    debug and print(f"\tRemoving directory: {expandedDir}")
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
#        os.utime(aPath, (result.st_atime + secondsIncrement, result.st_mtime + secondsIncrement))
        result = os.stat(aPath)
        print(f"\tNew mtime: \t{CommonFileSystem.convertEpochToHumanDate(int(result.st_mtime))} - ({int(result.st_mtime)}) for {aPath}")

        return int(result.st_mtime)
        

    def convertEpochToHumanDate(epochTime):

        
        mtime = time.gmtime(epochTime)
        #time.struct_time(tm_year=2012, tm_mon=8, tm_mday=28, tm_hour=0, tm_min=45, tm_sec=17, tm_wday=1, tm_yday=241, tm_isdst=0)  

        humanTime = time.strftime('%m/%d/%Y %H:%M:%S',  time.gmtime(epochTime))
        # '08/28/2012 00:45:17'

        return humanTime



    def executeCscour(deleteFlag, ingestFile, excludesFile):
        
        outPutfile = "/tmp/scour_log.txt"

        print(f"\t./scour -v  -d -e {excludesFile}  {ingestFile}") 
#, stdout=scourLog)
        if deleteFlag == "-d":
            with open(outPutfile, "w+") as scourLog:
                process = subprocess.run(["./scour", "-v",  "-d", "-e", excludesFile, ingestFile]) #, stdout=scourLog)
    
            #cmd = f"./scour -v -d {ingestFile} > {outPutfile}"
            #os.system(cmd)
        

    def unwrapIt(text):
        s=""
        for i in text:
            #if chr(i) == '\n': continue
            
            s+= chr(i)
        
        return s
        


# Scenario: 
#    - Create a directory tree
#    - Create a symlink to a file eligible for deletion
#    - Create  a directory symlink
#     Expected:
#    - file gets deleted and its symlink too
#    - directory becomes empty (all its scoured files are deleted OR some files remain)
#   - directory (and its symlink) is not deleted.

class SymlinkDeletion:    

    fileList=[    "/tmp/vesuvius/.scour$*.txt", 
                "/tmp/vesuvius/precipitation.foo",
                "/tmp/vesuvius/precipitation.txt",
                "/tmp/etna/precipitation.txt",
                "/tmp/etna/alt_dir/precipitation.txt",
                "/tmp/etna/vesuvius.foo",
                "/tmp/etna/.scour$*.foo"
    ]
    fileToSymlinkDict={
                "/tmp/etna/precipitation.txt": "/tmp/vesuvius/sl_etna_file",
                "/tmp/etna/alt_dir": "/tmp/vesuvius/sl_etna_alt_dir"
    }

    def __init__(self):
        
        self.ingestFile     = "/tmp/scourTest.conf"
        #self.entries         = "/tmp/vesuvius        2    *.txt\n/tmp/etna        7-1130    *.foo"
        self.entries         = "/tmp/vesuvius        2    *.txt"
        self.daysOldInSecs     = 172800             # seconds: 2 * 24 * 3600 s

        # Create the scour config file with entries 
        # for scour to use as CLI argument
        f = open(self.ingestFile, "w+")
        f.write(self.entries)
        f.close()

        self.excludes  = "/tmp/excludes.conf"
        self.entries   = "/tmp/vesuvius/exclude_me"
        
        # Create the excludes file with one entry 
        # for scour to use as CLI argument for -e option
        f = open(self.excludes, "w+")
        f.write(self.entries)
        f.close()


    def __str__(self):
        return f"{self.filename}  {self.timestamp}"

    def __eq__(self, other):
        if self.filename == other.filename:
            return true
        return false

    def createSymlinks(self, debug):

        debug and print(f"\nScour config file entrie(s): \n\t--> {self.entries}")

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
        #     expandedTarget =  os.path.expanduser(kTarget)
        #     expandedLink =  os.path.expanduser(vLink)
        #     CommonFileSystem.changeMTime(expandedTarget, daysOldInSecs)

        # make .scour$*.txt NEWER than precipitation.txt
        # Expect precipitation.txt to be deleted.
        
    #    scourFile=os.path.expanduser("/tmp/vesuvius/.scour$*.txt")
    #    CommonFileSystem.changeMTime(scourFile, daysOldInSecs)


        # make precipitation.txt OLDER than 2days +50secs
        # Expect precipitation.txt to be deleted.
        aFile=os.path.expanduser("/tmp/vesuvius/precipitation.txt")
        CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)


        aFile=os.path.expanduser("/tmp/etna/alt_dir/precipitation.txt")
        CommonFileSystem.changeMTime(aFile, daysOldInSecs +50)


        # make precipitation.txt OLDER than 4 days +50secs
        # Expect precipitation.txt to be deleted.
        aFile=os.path.expanduser("/tmp/etna/precipitation.txt")
        CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)



    def createFiles(self, debug):
        debug and print("\nCreate files:")
        CommonFileSystem.createFiles(self.fileList, debug)


    def runScenario(self, deleteFlag, debug):
        self.createFiles(debug)
        self.createSymlinks(debug)
        self.markEligibleForDeletion(self.daysOldInSecs)
        
        if debug:
            yes_no = input("Execute scour?")
            if yes_no == "n":
                exit(0)

        print(f"\n\tExecuting Scour...\n")
        CommonFileSystem.executeCscour(deleteFlag, self.ingestFile, self.excludes)
        
        print(f"\n\tRunning assertions...\n")
        return self.assertSymlink()


    def assertSymlink(self):
        
        status=0

        # Files:
        print(f"\nFILES: --------------------------------------")
        aPath=os.path.expanduser("/tmp/etna/alt_dir/precipitation.txt")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
            status=1

        aPath=os.path.expanduser("/tmp/vesuvius/precipitation.txt")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
            status=1

        aPath=os.path.expanduser("/tmp/etna/precipitation.txt")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
            status=1

        # Symlinks
        
        # "/tmp/etna/precipitation.txt": "/tmp/vesuvius/sl_etna_file",
        # "/tmp/etna/alt_dir": "/tmp/vesuvius/sl_etna_alt_dir"
        print(f"\nSYMLINKs: --------------------------------------")
        kTarget    = "/tmp/etna/precipitation.txt"
        vLink     = "/tmp/vesuvius/sl_etna_file"
        expandedTarget =  os.path.expanduser(kTarget)
        expandedLink =  os.path.expanduser(vLink)


        print(f"\t{expandedLink} --> {expandedTarget}")
        if     not os.path.exists(expandedTarget)    \
            and not os.path.islink(expandedLink):
            
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_LINK_DELETED}: {expandedLink}")            
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {expandedTarget}")
        else:
            if os.path.exists(expandedTarget)    \
                or os.path.islink(expandedLink):
                    
                print(f"\n\t{ASSERT_FAIL}: \tUn-Expected SymLink behavior! ({expandedTarget} - {expandedLink})  ")
                status=1    

        # "/tmp/etna/alt_dir": "/tmp/vesuvius/sl_etna_alt_dir"
        kTarget    = "/tmp/etna/alt_dir"
        vLink     = "/tmp/vesuvius/sl_etna_alt_dir"
        expandedTarget =  os.path.expanduser(kTarget)
        expandedLink =  os.path.expanduser(vLink)

        print(f"\t{expandedLink} --> {expandedTarget}")
        # Do not delete directory and its symlink
        if     os.path.exists(expandedTarget)         \
            and os.path.isdir(expandedTarget)     \
            and os.path.exists(expandedLink)     \
            and os.path.islink(expandedLink): 

            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_LINK_NOT_DELETED}: {expandedTarget}")                

        else:
            if     not os.path.exists(expandedTarget)         \
                or not os.path.islink(expandedLink): 

                print(f"\t{ASSERT_FAIL}: \t{ASSERT_DIR_LINK_NOT_DELETED}: {expandedTarget}")                
    
                status=1

        return status    


# Scenario: 
#    - Create a directory tree with files that all eligible for deletion (no symlink)
#    - Expected: all tree gets deleted

class EmptyDirectoriesDeletion:

    fileList=["/tmp/etna/precipitation.txt",
                "/tmp/etna/alt_dir/precipitation.txt"
    ]

    dirList=[
            "/tmp/vesuvius",
            "/tmp/etna",
            "/tmp/etna/alt_dir"
    ]
    
    def __init__(self):
        
        self.ingestFile = "/tmp/scourTest.conf"
        #self.entries     = "/tmp/vesuvius        2    *.txt\n/tmp/etna        7-1130    *.foo"
        self.entries     = "/tmp/etna        2    *.txt\
         \n/tmp/dirDoesNotExist        2    *.txt"
        self.daysOldInSecs     = 172800             # seconds: 2 * 24 * 3600 s

        # Create the scour config file with entries 
        # for scour to use as CLI argument
        f = open(self.ingestFile, "w+")
        f.write(self.entries)
        f.close()

        self.excludes  = "/tmp/excludes.conf"
        self.entries   = "/tmp/vesuvius/exclude_me"
        
        # Create the excludes file with one entry 
        # for scour to use as CLI argument for -e option
        f = open(self.excludes, "w+")
        f.write(self.entries)
        f.close()

    def __str__(self):
        return f"{self.filename}  {self.timestamp}"

    def __eq__(self, other):
        if self.filename == other.filename:
            return true
        return false


    def createFiles(self, debug):

        debug and print("\nCreate files:")
        CommonFileSystem.createFiles(self.fileList, debug)


    def markEligibleForDeletion(self, daysOldInSecs):

        # Set mtime to appropriate value to have file(s) deleted
        
        # TO REMOVE:
        # "/tmp/etna/precipitation.txt",
        # "/tmp/etna/alt_dir/precipitation.txt",
        # "/tmp/etna",
        # "/tmp/etna/alt_dir"        

        # make precipitation.txt OLDER than 4 days +50secs
        # Expect precipitation.txt to be deleted.
        aFile=os.path.expanduser("/tmp/etna/precipitation.txt")
        CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)

        aFile=os.path.expanduser("/tmp/etna/alt_dir/precipitation.txt")
        CommonFileSystem.changeMTime(aFile, daysOldInSecs +50)


    def runScenario(self, deleteFlag, debug):
        self.createFiles(debug)
        self.markEligibleForDeletion(self.daysOldInSecs)
        
        print(f"\n\tExecuting scour(1)...\n")
        CommonFileSystem.executeCscour(deleteFlag, self.ingestFile, self.excludes)
        
        print(f"\n\tRunning assertions...\n")
        
        return self.assertDirectoriesRemoved()


    def assertDirectoriesRemoved(self):
        
        status=0

        # Files:
        print(f"\nFILES: --------------------------------------")
        
        aPath=os.path.expanduser("/tmp/etna/alt_dir/precipitation.txt")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
            status=1

        aPath=os.path.expanduser("/tmp/etna/precipitation.txt")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
            status=1

        # Directories
        print(f"\nDIRECTORIES: --------------------------------------")

        aPath=os.path.expanduser("/tmp/etna/alt_dir")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_DIR_DELETED}: {aPath}")
            status=1
        
        aPath=os.path.expanduser("/tmp/etna")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_DIR_DELETED}: {aPath}")
            status=1

        aPath=os.path.expanduser("/tmp/dirDoesNotExist")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_NOT_DIR}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_NOT_DIR}: {aPath}")
            status=1

        # Expect an EXCLUDED directory to be still not deleted!
        aPath=os.path.expanduser("/tmp/vesuvius/exclude_me")
        if not os.path.exists(aPath): 
            print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_EXCLUDED_DIR_NOT_DELETED}: {aPath}")
        else:
            print(f"\t{ASSERT_FAIL}: \t{ASSERT_EXCLUDED_DIR_NOT_DELETED}: {aPath}")
            status=1
        return status

def main():

        cliParser = argparse.ArgumentParser(
            prog='testCscour.py',
            description='''This file is the test file for scour(1) .''',
            usage='''%(prog)s [-x]\n''',
            epilog='''

            '''
            )
        cliParser.add_argument('-x', action='store_true', help='debug')
        args = cliParser.parse_args()
        debug = args.x
        debug and system('clear')


        debug and print("\n\t========= Symlink Deletion scenario ================\n")
        
        debug and print("\nTear down directories:")
        CommonFileSystem.removeDirectories(debug)
        debug and print("\nCreate directories:")
        CommonFileSystem.createDirectories(debug)
        sl = SymlinkDeletion()
        sl_status = sl.runScenario("-d", debug)


        print("\n\t========== EmptyDirectories Deletion scenario =======\n")
        
        debug and print("\nTear down directories:")
        CommonFileSystem.removeDirectories(debug)
        debug and print("\nCreate directories:")
        CommonFileSystem.createDirectories(debug)
        ed = EmptyDirectoriesDeletion()
        ed_status = ed.runScenario("-d", debug)
        
        debug and print(f"sl_status: {sl_status} ed_status: {ed_status}")

        exit( ed_status * sl_status )

if __name__ == '__main__':
        main()

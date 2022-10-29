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


import  os
from    os      import system
from    os      import path
from    pathlib import Path
import  time
import  subprocess
from    subprocess import PIPE
import  sys
import  errno
import  shutil
import  argparse

ASSERT_FILE_DELETED             = "Expect file to be deleted"
ASSERT_FILE_NOT_DELETED         = "Expect file NOT to be deleted"
ASSERT_SYMLINK_FILE_NOT_DELETED = "Expect file and its symlink be deleted"
ASSERT_SYMLINK_DIR_NOT_DELETED  = "Expect directory and its symlink NOT be deleted"
ASSERT_SYMLINK_DIR_DELETED      = "Expect directory and its symlink be deleted"
ASSERT_DIR_DELETED              = "Expect directory to be deleted"
ASSERT_DIR_NOT_DELETED          = "Expect directory NOT be deleted"
ASSERT_NOT_DIR                  = "Expect 'not-a-directory' to be skipped"
ASSERT_EXCLUDED_DIR_NOT_DELETED = "Expect directory to be excluded"
ASSERT_SUCCESS                  = "SUCCESS"
ASSERT_FAIL                     = "FAIL"

class CommonFileSystem:
    
    def __init__(self):
        pass

    def createFiles(fileList, debug):

        for file in fileList:
            expandedFile =  os.path.expanduser(file)
            debug and print(f"--> {expandedFile}")
            if not os.path.exists(expandedFile):
                debug and print(f"\tCreating file: {expandedFile}")
                f = open(expandedFile, "w+")
                f.close()

        # Initialize timestamp to zero
        fileDict={}.fromkeys(fileList,0)
        return fileDict

    def createDirectories(dirList, debug):

        for directory in dirList:
            
            expandedDir =  os.path.expanduser(directory)
            if os.path.exists(expandedDir):
                debug and print(f"\tDirectory {expandedDir} exists.")
                continue

            try:
                debug and print(f"\tCreating directory: {expandedDir}")
                os.mkdir(expandedDir)
            except OSError as e:
                print(e)
            

    def removeDirsAndFiles(dirList, fileList, debug):

        for directory in dirList:
            
            expandedDir =  os.path.expanduser(os.path.expandvars(directory))
            try:
                if os.path.exists(expandedDir):
                    debug and print(f"\tRemoving directory: {expandedDir}")
                    shutil.rmtree(expandedDir)        
            except OSError as e:
                print(e)
            

        for file in fileList:
            #excludes = "/tmp/excludes.conf"
            try: 
                os.remove(file)
            except:
                pass

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

        print(f"\t./scour -v  -d -e {excludesFile}  {ingestFile}") #, stdout=scourLog)
        if deleteFlag == "-d":
            with open(outPutfile, "w+") as scourLog:
                process = subprocess.run(["./scour", "-v", "-d", "-e", excludesFile, ingestFile]) #, stdout=scourLog)
        else:
            with open(outPutfile, "w+") as scourLog:
                process = subprocess.run(["./scour", "-v",  "-e", excludesFile, ingestFile]) #, stdout=scourLog)
    

        # Alternately:
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
#    - directory (and its symlink) is not deleted.

class SymlinkDeletion:    

    dirList =[      # to create
        "/tmp/vesuvius",                        # should NOT be deleted
        "/tmp/vesuvius/exclude_me",             # should NOT be deleted (from /tmp/excludes.txt)
        "/tmp/vesuvius/symlinked_target_dir",          # should NOT be deleted
        "/tmp/vesuvius/dir_to_delete_old",          # should be deleted when empty 
        "/tmp/vesuvius/dir_toNOT_delete"        # should NOT be delete (not empty)
    ]
    
    fileList=[      # to create
        "/tmp/vesuvius/.scour$*.txt",                   # should be deleted
        "/tmp/vesuvius/precipitation.foo",              # should be deleted
        "/tmp/vesuvius/precipitation.txt",              # should be deleted
        "/tmp/vesuvius/symlinked_file.txt",             # should be deleted
        "/tmp/vesuvius/dir_to_delete_old/old_images.txt",   # should be deleted  
        "/tmp/vesuvius/dir_toNOT_delete/new_images.txt" # should NOT be deleted  
    ]

    fileToSymlinkDict={      # create their symlink
        "/tmp/vesuvius/symlinked_file.txt"  : "/tmp/vesuvius/sl_symlinked_file.txt",    # both symlink and file should be deleted
        "/tmp/vesuvius/symlinked_target_dir"       : "/tmp/vesuvius/sl_symlinked_dir"          # NONE should be deleted
    }

    deleteList=[   # mark them OLD (change their mtime)
        "/tmp/vesuvius/.scour$*.txt",                   # should be deleted
        "/tmp/vesuvius/precipitation.foo",              # should be deleted
        "/tmp/vesuvius/precipitation.txt",              # should be deleted
        "/tmp/vesuvius/symlinked_file.txt",             # should be deleted
        "/tmp/vesuvius/dir_to_delete_old/old_images.txt"    # should be deleted  
    ]

    # ASSERTIONS lists:

    # ========================== FILES ====================================
    expectedDeletedFilesList = [
        "/tmp/vesuvius/.scour$*.txt",                   # should be deleted
        "/tmp/vesuvius/precipitation.txt",              # should be deleted
        "/tmp/vesuvius/dir_to_delete/old_images.txt"    # should be deleted          
    ]


    expectedNotDeletedFilesList = [
        "/tmp/vesuvius/dir_toNOT_delete/new_images.txt", # should NOT be deleted
        "/tmp/vesuvius/precipitation.foo"                # should NOT be deleted (not the concerned pattern: '*.txt')
    ]


    # ========================== SYMLINKS ==================================
    # DOES NOT WORK! Hence the inversion in the test condition: not
    expectedNotDeletedSymlinkDirsList = [
        "/tmp/vesuvius/symlinked_target_dir",              # should NOT be deleted
        "/tmp/vesuvius/sl_symlinked_dir"            # should NOT be deleted
    ]


    expectedDeletedSymlinkedFilesList = [
        "/tmp/vesuvius/symlinked_file.txt",         # symlink target should be deleted: already in files list to delete
        "/tmp/vesuvius/sl_symlinked_file.txt"       # symlink        should be deleted
    ]


    # ========================== DIRECTORIES =================================
    expectedDeletedDirsList = [
        "/tmp/vesuvius/dir_to_delete_old"           # should be deleted when empty
    ]

    expectedNotDeletedDirsList = [
        "/tmp/vesuvius",                            # should NOT be deleted
        "/tmp/vesuvius/dir_toNOT_delete"            # should NOT be delete (not empty, contains images.txt)
    ]

    expectedNotDeletedExcludedDirsList = [
        "/tmp/vesuvius/exclude_me"                  # should NOT be deleted (from /tmp/excludes.txt)
    ]
      

    expectedNonExistentDirectorySkippedList = [
        "/tmp/vesuvius/dirDoesNotExist"                  # should NOT be deleted (from /tmp/excludes.txt)
    ]


    def __init__(self, debug):
        
        self.ingestFile = "/tmp/scourTest.conf"
        entry1          ="/tmp/vesuvius        2    *.txt"
        entry2          ="/tmp/dirDoesNotExist"
        entry3          ="/tmp/etna        7-1130    *.foo"   # etna not created
        self.entries    = f"{entry1}\n{entry2}"
        self.daysOldInSecs = 172800             # seconds: 2 (days) * 24 * 3600 s

        # 1. Create the scour config file with entries 
        f = open(self.ingestFile, "w")
        f.write(self.entries)
        f.close()

        # 2. Create the to-be-excluded files conf-file
        self.excludes  = "/tmp/excludes.conf"
        self.entries   = "/tmp/vesuvius/exclude_me"
        f = open(self.excludes, "w")
        f.write(self.entries)
        f.close()

        # 3. Prepare the scenario structure
        #
        # 3.1 Tear down
        debug and print("\nTear down directories:")
        CommonFileSystem.removeDirsAndFiles(self.dirList, self.fileList, debug)
        
        # 3.2 Create directories
        debug and print("\nCreate directories:")
        CommonFileSystem.createDirectories(self.dirList, debug)

        # 3.3 Create files
        self.createFiles(debug)

        # 3.4 Create symlinks
        self.createSymlinks(debug)

        # 3.5 Timestamp files eligible for deletion 
        self.markEligibleForDeletion(self.deleteList, self.daysOldInSecs)

    
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


    def markEligibleForDeletion(self, electedFiles, daysOldInSecs):

        # Set mtime to appropriate value to have file deleted
        for file in electedFiles:

            # make precipitation.txt OLDER than 2days +50secs
            # Expect precipitation.txt to be deleted.
            aFile=os.path.expanduser(file)
            CommonFileSystem.changeMTime(aFile, 2 * daysOldInSecs +50)

    def createFiles(self, debug):
        debug and print("\nCreate files:")
        CommonFileSystem.createFiles(self.fileList, debug)


    def runScenario(self, deleteFlag, debug):

        if debug:
            yes_no = input("Execute scour? (yes is default) ")
            if yes_no == "n":
                exit(0)

        print(f"\n\tExecuting Scour...\n")
        CommonFileSystem.executeCscour(deleteFlag, self.ingestFile, self.excludes)
        
        print(f"\n\tRunning assertions...\n")
        return self.assertSymlink()



    def assertSymlink(self):
    
        status=0

        # ========================== FILES ====================================
        print(f"\nFILES: --------------------------------------")
        # expectedDeletedFilesList
        for file in self.expectedDeletedFilesList:
            aPath=os.path.expanduser(file)
            if not os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_DELETED}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_DELETED}: {aPath}")
                status=1

        print("")

        # expectedNotDeletedFilesList
        for file in self.expectedNotDeletedFilesList:
            aPath=os.path.expanduser(file)
            if os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_FILE_NOT_DELETED}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_FILE_NOT_DELETED}: {aPath}")
                status=1

        # ========================== SYMLINKS ==================================
        print(f"\nSYMLINKs: --------------------------------------")

        # expectedDeletedSymlinkedFilesList
        for file in self.expectedDeletedSymlinkedFilesList:
            # print(f"\t{file} --> {expectedDeletedSymlinkedFilesList[file]}")

            aPath=os.path.expanduser(file)
            if not os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_SYMLINK_FILE_NOT_DELETED}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_SYMLINK_FILE_NOT_DELETED}: {aPath}")
                status=1
        
        print("")
        
        # expectedNotDeletedSymlinkDirsList
        for file in self.expectedNotDeletedSymlinkDirsList:
    
            aPath=os.path.expanduser(file)
            if not os.path.exists(aPath): 
                #print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_SYMLINK_DIR_NOT_DELETED}: {aPath}")
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_SYMLINK_DIR_DELETED}: {aPath}")
            else:
                #print(f"\t{ASSERT_FAIL}: \t{ASSERT_SYMLINK_DIR_NOT_DELETED}: {aPath}")
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_SYMLINK_DIR_DELETED}: {aPath}")
                status=1


        # ========================== DIRECTORIES =================================
        print(f"\nDIRECTORIES: --------------------------------------")
        # expectedDeletedDirsList
        for file in self.expectedDeletedDirsList:
            aPath=os.path.expanduser(file)
            
            if not os.path.exists(aPath): 
                
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_DELETED}: {aPath}")
            else:
                
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_DIR_DELETED}: {aPath}")
                status=1

        print("")

        # expectedNotDeletedDirsList
        for file in self.expectedNotDeletedDirsList:
            aPath=os.path.expanduser(file)
            if os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_DIR_NOT_DELETED}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_DIR_NOT_DELETED}: {aPath}")
                status=1

        # ========================== EXCLUDED DIRECTORIES =================================
        print(f"\nEXCLUDED dirs: --------------------------------------")

        # expectedNotDeletedExcludedDirsList
        for file in self.expectedNotDeletedExcludedDirsList:
            aPath=os.path.expanduser(file)
            if os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_EXCLUDED_DIR_NOT_DELETED}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_EXCLUDED_DIR_NOT_DELETED}: {aPath}")
                status=1


        print("")
        # 'not-a-directory' (directory does not exists: no scouring BUT no system crash either.)
        #  ASSERT_NOT_DIR    = "Expect 'not-a-directory' to be skipped from scouring"

        # ========================== NON-EXISTENT DIRECTORIES: to escape scouring ===========
        print(f"\nSkip scouring non-existent dirs: ---------- (expect no crash) ------")

        # expectedNonExistentDirectorySkippedList
        for file in self.expectedNonExistentDirectorySkippedList:
            aPath=os.path.expanduser(file)
            if not os.path.exists(aPath): 
                print(f"\t{ASSERT_SUCCESS}: \t{ASSERT_NOT_DIR}: {aPath}")
            else:
                print(f"\t{ASSERT_FAIL}: \t{ASSERT_NOT_DIR}: {aPath}")
                status=1


        print("\n\n")
        


        return status    

def main():

        print(f"\nParsing arguments")
        cliParser = argparse.ArgumentParser(
            prog='testCscour.py',
            description='''This file is the test file for scour(1) .''',
            usage='''%(prog)s [-x]\n''',
            epilog='''

            '''
            )
        print(f"\nAdding argument")
        cliParser.add_argument('-x', action='store_true', help='debug')
        args    = cliParser.parse_args()
        debug   = args.x
        debug and system('clear')
        
        print(f"\nDeleting symlink")
        sl          = SymlinkDeletion(debug)
        sl_status   = sl.runScenario("-d", debug)

        exit( sl_status )

        

if __name__ == '__main__':
        main()

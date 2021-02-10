/**
 * This file declares the API for mapping from unit systems to their associated
 * pointers for version 2 of the Unidata UDUNITS package.
 *
 *  @file:  Cscour.c
 * @author: Mustapha Iles
 *
 *    Copyright 2021 University Corporation for Atmospheric Research
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <pwd.h>
#include <pthread.h>
#include <libgen.h>
#include <fnmatch.h>

#include "Cscour.h"
#include "parser.h"
/*
# Configuration file for "scour" utility, to delete all files older than a
# specified number of days from specified directories and all their
# subdirectories.  Scour should be invoked periodically by cron(8).
#
*/

#define STAR_CHAR               "*"

// pass 2 arguments to threaded function
typedef struct config_items_args {
    
    char    dir[DIR_SIZE];
    int     daysOldInEpoch;         // daysOld: 1-hhmmss converted to Epoch 
    char    daysOld[DAYS_OLD_SIZE];
    char    pattern[PATTERN_SIZE]; 
    int     deleteDirsFlag;

    pthread_t threadId;             // for convenience

} ConfigItemsAndDeleteFlag_t;

int verbose=0;
char *ingestFilename;


int main(int argc, char *argv[])
{
    
    int deleteDirsFlag;

    parseArgv(argc, argv, &deleteDirsFlag, &verbose);
    
    verbose && printf("\n\n\tCscour: STARTED...\n\n");
    verbose && printf("\n\n\t == Cscour: parsing...\n\n");

    // Call config parser
    int validEntriesCounter;
    IngestEntry_t *listHead = parseConfig(&validEntriesCounter);
    
    verbose && printf("\n\t == Cscour: parsing complete!\n\n");

    // enable for debug only:
    //traverseIngestList(listHead);

    verbose && printf("\n\t == Cscour: Launching %d threads...\n\n", 
                        validEntriesCounter);

    multiThreadedScour(listHead, deleteDirsFlag);
 
    verbose && printf("\n\n\tCscour: Complete!\n\n");

    exit(0);
}

void multiThreadedScour(IngestEntry_t *head, int deleteDirsFlag)
{

    IngestEntry_t *tmp = head;
    if(head) 
        verbose && printf("\n\tCscour: List of validated items sourced in user's configuration file: %s\n", 
            ingestFilename);
    
    pthread_t tids[MAX_THREADS];
    int threadsCounter  =   0;

    ConfigItemsAndDeleteFlag_t *items;
        

   //start from the beginning of the config parsed list
   while(tmp != NULL) 
   {
        //TO-DO:  check threads limit
        //checkThreadsMax();

        // items's memory allocation is made in this function but freed in another 
        // function: "scourFilesAndDirsForThisPath", executed by a thread. 
        items = (ConfigItemsAndDeleteFlag_t *) 
                        malloc(sizeof(ConfigItemsAndDeleteFlag_t));

        verbose && printf("\n\t====> Processing directory:%s with daysOld: %s (%d) and pattern: %s\n",
            tmp->dir, tmp->daysOld, tmp->daysOldInEpoch, tmp->pattern);

        strcpy(items->dir,      tmp->dir);
        items->daysOldInEpoch =  tmp->daysOldInEpoch;
        strcpy(items->daysOld,  tmp->daysOld);
        strcpy(items->pattern,  tmp->pattern);     
        items->deleteDirsFlag = deleteDirsFlag;     // delete or not delete empty directories

        // Create attributes & init
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_create(&tids[threadsCounter++], &attr, 
                    scourFilesAndDirsForThisPath, items);
        
        tmp = tmp->nextEntry;
    }

    // wait until the thread is done executing
    tmp = head;
    int i=0;
    while(tmp != NULL) 
    {
        // Thread ID:
        pthread_join(tids[i++], NULL);
        verbose && printf("\nScouring directory (%s) completed with thread ID counter: %d!\n", 
                    tmp->dir, i-1);
        
        tmp = tmp->nextEntry; 
    } 
}

// The lower the epoch time the older the file
static int isThisOlderThanThat(int thisFileEpoch, int thatFileEpoch)
{
    return (thisFileEpoch <= thatFileEpoch);
}

static int removeFile(char *path, char * daysOld)
{
    int status = remove(path);

    // current file is OLDER than daysOld
    if (status)
    {
        verbose && printf("removeFile(\"%s\") failed.\n", path);
    }
    else
    {
        // log_info("\t(+)File \"%s\" is older than %s (days[-HHMMSS]) - DELETED!\n", path, daysOld);
        verbose && printf("\t(+)File \"%s\" is OLDER than %s (days[-HHMMSS]) - DELETED!\n",
                        path, daysOld);
    }
    return status;
}

int isSymlinkDirectory(char *path)
{
    struct stat sb;
    if (stat(path, &sb) == -1)
    {
        verbose && printf("\tisSymlinkDirectory: symlink \"%s\"  is broken! Removing it...\n",
            path);
        unlink(path);
        return 0;
    }
    return S_ISDIR(sb.st_mode);
}

// delete the symlink if target file  is older than daysOld, so that symlink is not left broken
static int removeFileSymlink(char *symlinkPath, char *symlinkedEntry, int daysOldInEpoch,
                     int deleteDirsFlag, char *daysOld)
{
    char symlinkedFileToRemove[PATH_MAX];

    struct stat sb;
    if (stat(symlinkedEntry, &sb) == -1)
    {
        printf("stat(\"%s\") failed.\n", symlinkedEntry);
        return -1;
    }

    int targetedFileEpoch = sb.st_mtime;
    if( isThisOlderThanThat(targetedFileEpoch, daysOldInEpoch) ) {
        remove(symlinkedEntry);
        // and remove the symlink too:
        remove(symlinkPath);
    }
    return 1;
}

// This is the recursive function to traverse the directory tree, depth-first
static
int scourFilesAndDirs(char *basePath, int daysOldInEpoch,
                      char *pattern,  int deleteDirsFlag,
                      char *daysOld,  int symlinkFlag)
{
    
    struct dirent *dp;
    
    DIR *dir = opendir(basePath);
    // Unable to open directory stream
    if(!dir) 
    {
        fprintf(stderr, "failed to open directory \"%s\" (%d: %s)\n",
                basePath, errno, strerror(errno));
        return -1;
    }

    int dfd = dirfd(dir);
     
    while ((dp = readdir(dir)) != NULL) 
    {
        struct stat sb;
        if (fstatat(dfd, dp->d_name, &sb, AT_SYMLINK_NOFOLLOW) == -1) 
        {
            fprintf(stderr, "fstatat(\"%s/%s\") failed: %s\n",
                basePath, dp->d_name, strerror(errno));
            return -1;
        }

        int currentEntryEpoch = sb.st_mtime;                

        // new basePath is either a dir, a file, or a link to follow if it's a directory symlimk
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", basePath, dp->d_name);
        

        switch (sb.st_mode & S_IFMT) 
        {
        case S_IFDIR :
                
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                continue;
        
            verbose && printf("\n(d) %s\n", dp->d_name);
            
            // depth-first traversal
            scourFilesAndDirs(path, daysOldInEpoch, pattern, deleteDirsFlag, daysOld, symlinkFlag);

            
            // Remove if empty and not symlinked
            if( isDirectoryEmpty(path) && !symlinkFlag && deleteDirsFlag)
            {

        // ===========================  TO-DO:  remove dangling REG file Symlinks HERE ! ===============================
                                
                verbose && printf("\tDeleting this (empty) directory %s if older than %s (days[-HHMMSS]) (epoch: %d)\n\n", 
                                path, daysOld, daysOldInEpoch);
                if( isThisOlderThanThat( currentEntryEpoch, daysOldInEpoch) )
                {
                    if(remove(path))
                    {
                        verbose && fprintf(stderr, "\n\tdirectory remove(\"%s\") failed: %s\n",
                            path, strerror(errno));
                            break;
                    }                
                }
                else
                {
                    verbose && printf("\tDirectory %s if NOT older than %s (days[-HHMMSS])\n\n", 
                        path, daysOld);
                }

            } 

            break;

        case S_IFREG :

            verbose && printf("\n(r) %s\n", path);
                  
            // Only examine pattern-matching files and non-.scour files 
            // fnmatch returns 0 if match found
            if( fnmatch(pattern, dp->d_name, FNM_PATHNAME)  )
            {
                verbose && printf("\t(-) File \"%s\" does NOT match pattern: %s\n",  dp->d_name, pattern);   
                continue;   
            }
            verbose && printf("\t(+) File \"%s\" matches pattern: %s\n",  dp->d_name, pattern);

            if ( isThisOlderThanThat(currentEntryEpoch, daysOldInEpoch) )
            {
                removeFile(path, daysOld);
                continue;
            }    
                
            verbose && printf("\t(-) File \"%s\" is NOT older than %s (days[-HHMMSS]) - Skipping it...\n", 
                        path, daysOld);                        
    
            break;

        case S_IFLNK:
        
            verbose && printf("\n(sl) %s\n", path);

            char symlinkedEntry[PATH_MAX];
            callReadLink(path, symlinkedEntry);

            if(isSymlinkDirectory(path))
            {
                verbose && printf("\t(d) Following symlink: %s (Will not be removed.)\n", symlinkedEntry);
                scourFilesAndDirs(symlinkedEntry, daysOldInEpoch, pattern, 
                                    deleteDirsFlag, daysOld, IS_DIRECTORY_SYMLINK);

                // Directories of a SYMLINK will not get removed.
            }
            else
            {
                verbose && printf("\n\t(-sl) %s is a linked file. Remove if OLDER than %s daysOld (days[-HHMMSS])\n", 
                            symlinkedEntry, daysOld);
                // delete the symlink if target file  is older than daysOld, so that symlink is not left broken
                // however, currentEntryEpoch should NOT be that of the symlink but that of the file pointed to by the slink
                removeFileSymlink(path, symlinkedEntry, daysOldInEpoch, deleteDirsFlag, daysOld);
            } 
            break;

        default: 

            verbose && printf("\n(?) NOT a regular file, nor a symlink: \"%s\"\n", dp->d_name);
            
            break;
        }
    }
    closedir(dir);
    return 0;
}

// This is the thread function
void* scourFilesAndDirsForThisPath(void *oneItemStruct)
{

    ConfigItemsAndDeleteFlag_t currentItem = *(ConfigItemsAndDeleteFlag_t *)
                                                oneItemStruct;

    char *dirPath           = currentItem.dir;
    char *daysOld           = currentItem.daysOld;     // <days>[-HHMMSS], eg. 1-122033
    int   daysOldInEpoch    = currentItem.daysOldInEpoch;     // parsed from <days>[-HHMMSS], eg. 1-122033 to Epoch time
    char *pattern           = currentItem.pattern;

    int   deleteDirOrNot    = currentItem.deleteDirsFlag;

    // free memory of the struct that was allocated in the calling function: multiThreadedScour()
    free((ConfigItemsAndDeleteFlag_t *) oneItemStruct);

    // scour candidate files and directories under 'path' - recursively
    // assume that this first entry directory is NOT a synbolic link
    // delete empty directories if delete option (-d) is set
    scourFilesAndDirs(  dirPath, daysOldInEpoch, pattern,
                        deleteDirOrNot, daysOld, IS_NOT_DIRECTORY_SYMLINK);

    // after bubbling up , remove directory if empty and if delete option is set
    if( isDirectoryEmpty(dirPath) && deleteDirOrNot)
    {
        // .scour$pattern is removed too
        remove(dirPath);
    }

    // TO-DO:  remove dangling Symlinks


    pthread_exit(0);
}

void getFQFilename(char *dirPath, char *filename, char *FQFilename)
{
    char fullFilename[PATH_MAX];
    memset(fullFilename, '\0', sizeof(fullFilename));
    
    strcpy(fullFilename, dirPath);
    strcat(fullFilename, "/");
    strcat(fullFilename, filename);

    strncpy(FQFilename, fullFilename, strlen(fullFilename));

}

int epochOfLastModified(char *dirPath, char *aFile)
{
    char FQFilename[PATH_MAX];
    getFQFilename(dirPath, aFile, FQFilename);
    
    struct stat sb;
    if (stat(FQFilename, &sb) == -1) 
    {
        verbose && fprintf(stderr, "epochOfLastModified: stat(\"%s\") failed: %s\n", 
            FQFilename, strerror(errno));
        return -1;
    }

    return sb.st_mtime;
    
}

// check if directory is empty except for ,scour file. 
// If .scour file there remove it.
int isDirectoryEmpty(char* dirname)
{
    int n=0;
    struct dirent *dp;
    
    DIR *dir = opendir(dirname);
    while((dp = readdir(dir))!=NULL && n<4) 
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;
        n++;
    }
    closedir(dir);

    return n == 0? 1 : 0;
}

void callReadLink(char *path, char *target)
{
    char buf[PATH_MAX];
    size_t len;
    
    if ((len = readlink(path, buf, sizeof(buf)-1)) != -1)
    {
        buf[len] = '\0';
    }
    else {
        fprintf(stderr, "readlink(\"%s\") failed: %s\n", path, 
            strerror(errno));
    }
    strcpy(target, buf);
}

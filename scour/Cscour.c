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
    char    pattern[PATTERN_SIZE]; 
    int     deleteDirsFlag;

    pthread_t threadId;             // for convenience

} ConfigItemsAndDeleteFlag_t;

int verbose=0;
int main(int argc, char *argv[])
{
    
    int deleteDirsFlag;

    printf("\n\n\tCscour: STARTED...\n\n");
    printf("\n\n\t == Cscour: parsing...\n\n");
    parseArgv(argc, argv, &deleteDirsFlag, &verbose);
    
    // Call config parser
    int validEntriesCounter;
    IngestEntry_t *listHead = parseConfig(&validEntriesCounter);
    
    printf("\n\t == Cscour: parsing complete!\n\n");

    // enable for debug only:
    //traverseIngestList(listHead);


    printf("\n\t == Cscour: Launching %d threads...\n\n", validEntriesCounter);

    multiThreadedScour(listHead, deleteDirsFlag);
 

    printf("\n\n\tCscour: Complete!\n\n");

    exit(0);
}

void multiThreadedScour(IngestEntry_t *head, int deleteDirsFlag)
{

    IngestEntry_t *tmp = head;
    if(head) printf("\n\tCscour: List of validated items sourced in user's configuration file: %s\n", SCOUR_INGEST_FILENAME);
    
    pthread_t tids[MAX_THREADS];
    int threadsCounter  =   0;

    ConfigItemsAndDeleteFlag_t *items = (ConfigItemsAndDeleteFlag_t *) 
                        malloc(sizeof(ConfigItemsAndDeleteFlag_t));
        

   //start from the beginning of the config parsed list
   while(tmp != NULL) 
   {
        //TO-DO:  check threads limit
        //checkThreadsMax();

        printf("\t%s \t %d \t %s\n",tmp->dir, tmp->daysOldInEpoch, tmp->pattern);

        strcpy(items->dir,      tmp->dir);
        items->daysOldInEpoch,  tmp->daysOldInEpoch;
        strcpy(items->pattern,  tmp->pattern);
        
        items->deleteDirsFlag = deleteDirsFlag;

        // Create attributes & init
        pthread_attr_t attr;
        pthread_attr_init(&attr);

        pthread_create(&tids[threadsCounter++], &attr, 
                    scourFilesAndDirsForThisPath, items);
   
        tmp = tmp->nextEntry;
    }
    printf("\n");
    // wait until the thread is done executing
    tmp = head;
    int i=0;
    while(tmp != NULL) 
    {
        // Thread ID:
        pthread_join(tids[i++], NULL);
        verbose && printf("Scouring directory (%s) completed with thread ID counter: %d!\n", tmp->dir, i-1);
        
        tmp = tmp->nextEntry; 
    } 
}

// This is the thread function
void* scourFilesAndDirsForThisPath(void *oneItemStruct) 
{

    ConfigItemsAndDeleteFlag_t currentItem = *(ConfigItemsAndDeleteFlag_t *) oneItemStruct;

    char *dirPath   = currentItem.dir;
    int   daysOldInEpoch   = currentItem.daysOldInEpoch;     //string:  <days>[-HHMMSS], eg. 1-122033
    char *pattern   = currentItem.pattern;

    int   deleteDirOrNot = currentItem.deleteDirsFlag;

    // scour candidate files and directories under 'path' - recursively
    // delete empty directories and delete option (-d) set
    scourFilesAndDirs(dirPath,  daysOldInEpoch, pattern, deleteDirOrNot); 
    
    // remove top directory if empty and if delete option is set
    if( isDirectoryEmpty(dirPath) && deleteDirOrNot)
    {
        // TO-DO: remove .scour$pattern first.
        remove(dirPath);
    }
        

    pthread_exit(0); 
}

int scourFilesAndDirs(char *basePath,  int daysOldInEpoch, char *pattern, int deleteDirsFlag) 
{
    struct dirent *dp;
    struct stat statbuf;

    DIR *dir = opendir(basePath);

    // Unable to open directory stream
    if(!dir) 
    {
        fprintf(stderr, "failed to open directory \"%s\" (%d: %s)\n",
                *basePath, errno, strerror(errno));
        return -1;
    }

    int dfd = dirfd(dir);

    // check if already visited:
    if( !hasDirChanged(basePath) ) 
    {
        printf("Directory \"%s\" unchanged since scour's last visit\n\n", basePath);
        return 0;
    }    

    while ((dp = readdir(dir)) != NULL) {

        struct stat sb;
        if (fstatat(dfd, dp->d_name, &sb, 0) == -1) {
            fprintf(stderr, "fstatat(\"%s/%s\") failed: %s\n",
                basePath, dp->d_name, strerror(errno));
            return -1;
        }

        // new basePath is current basePath+d_name. 
        // And it is either a dir, a file, or a link to follow
        char path[PATH_SIZE];
        snprintf(path, sizeof(path), "%s/%s", basePath, dp->d_name);
        
        if (S_ISDIR(sb.st_mode)) {
            
            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                continue;
        
            printf("[%s]\n", dp->d_name);
            
            // depth-first traversal
            scourFilesAndDirs(path, daysOldInEpoch, pattern, deleteDirsFlag);

            if( isDirectoryEmpty(path) && deleteDirsFlag)
            {
// TO-DO
                printf("\tDeleting this (empty) directory if older than daysOld: %s\n\n", path);
                if(rmdir(path))
                {
                    fprintf(stderr, "rmdir(\"%s\") failed: %s\n",
                        path, strerror(errno));
                        return -1;
                }
            }
            else
            {
                printf("Directory not empty!: %s\n\n", path);
            }
        } 
        else if(S_ISREG(sb.st_mode)) 
            {   
                long epochLastModified = sb.st_ctime;
                printf("(r) %s\n", path);
                
                // if not matching the pattern, skip this file
                if( !fnmatch(pattern, dp->d_name, FNM_PATHNAME) ) 
                {
                    printf("File %s matches pattern: %s\n",  dp->d_name, pattern);
                }
                else
                {
                    printf("File %s does NOT match pattern: %s\n",  dp->d_name, pattern);   
                }
                
                //if( isOlderThan(daysOld, epochLastModified) )
                if( daysOldInEpoch >= epochLastModified) 
                {
                    if( !remove(path) ) 
                    {
                        printf("\t %s is older than %d days - DELETED!\n", path, daysOldInEpoch);
                    }
                    else {
                        fprintf(stderr, "rmdir(\"%s\") failed: %s\n",
                            path, strerror(errno));
                        return -1;
                    }
                }
                else
                {
                    printf("\t %s is NOT older than %d days - Not yet a candidate for deletion!\n", 
                        path, daysOldInEpoch);
                }
            }
        else if(S_ISLNK(sb.st_mode)) 
            {   
               
                printf("SYMLINK file: %s  !!!!!!!!!!!!!!!!!!!!\n", dp->d_name);
            }
        else 
            {   
               
                printf("NOT a regular file, nor a symlink: %s  !!!!!!!!!!!!!!!!!!!!\n", dp->d_name);
            }

    }

    closedir(dir);
}

int checkGlobPatternMatch(char *pattern, char *pathName)
{
    if(strcmp(pattern, STAR_CHAR) == 0) return 1;

    // extract filename: 
    char *filename;
    filename = basename(pathName);


}

int isOlderThan(int scouringDays, long epochLastModified)
{
    struct timespec timeNow;
    clock_gettime(CLOCK_REALTIME, &timeNow);

    long epochTimeNow = timeNow.tv_sec;
    int howOldSecs = epochTimeNow - epochLastModified;
    int howOldDays = howOldSecs / SECONDS_IN_1DAY;

    //printf( "How old (sec): %ld \t in days:%ld\n", howOldSecs, howOldDays);

    return howOldDays >= scouringDays? 1:0;

}

int isDirectoryEmpty(char* dirname)
{
    int n=0;
    struct dirent *dp;
    
    DIR *dir = opendir(dirname);
    while((dp = readdir(dir))!=NULL && n<4) 
    {
        if( strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0 
            || strcmp(dp->d_name, SCOUR_FILENAME) == 0)
        {
            continue;
        }
        n++;
    }
    closedir(dir);
    
    return n == 0? 1 : 0;
}

int hasDirChanged(char * currentDirPath)
{
    struct stat stats;
    struct stat dirstats;

    const char* scourFilename = SCOUR_FILENAME;
    char scourPath[PATH_SIZE];
    strcpy(scourPath, currentDirPath);
    strcat(scourPath, scourFilename);

    // check if scour exists, if not create it.
    if(stat(scourPath, &stats)  < 0)
    {
        printf("stat(\"%s\") failed, thus, creating .scour file in %s\n", 
            scourPath, currentDirPath);

        FILE *fp;
        if((fp = fopen(scourPath, "w")) == NULL)
        {
            printf("Unable to create .scour: %s\n", scourPath);
            return 0;
        }

        // treat this directory as if it has changed (although we cannot tell)
        // its ctime will be that of .scour newly creaate file
        fclose(fp);
        return 1;   
    }
    int epochDotScourFileChanged = stats.st_ctime;
    epochPrettyPrinting(scourPath, epochDotScourFileChanged);    

    // Stat this current directory:
    if(stat(currentDirPath, &dirstats) < 0)
    {
        fprintf(stderr, "stat(\"%s\") failed: %s\n",
            currentDirPath, strerror(errno));
        return -1;
    }
    int epochLastCurrentDirChanged = dirstats.st_ctime;
    epochPrettyPrinting(currentDirPath, epochLastCurrentDirChanged);    

    // same epoch time: returns 0, directory did not change
    if(dirstats.st_ctime <= stats.st_ctime) {
        return 0;
    }

    // dir has changed: update .scour's ctime to that of dir
    // It is not possible to change the ctime/mtime of a file: so recreate it!
    //stats.st_ctime = dirstats.st_ctime;// epochLastCurrentDirChanged;

    if( remove(scourPath) < 0 )
    {
        fprintf(stderr, "remove(\"%s\") failed: %s\n", 
            scourPath, strerror(errno));
        return -1;
    }

    FILE *fp;
    if((fp = fopen(scourPath, "w")) == NULL)
    {
        printf("Unable to create .scour: %s\n", scourPath);
        return 0;
    }
    fclose(fp);

    // By returning 1, treat this directory as if it has changed 
    // (although we cannot tell)
    // its ctime will be that of .scour newly create file (or greater)
    return 1;
}

void epochPrettyPrinting(char * fileOrDir, time_t epochCTime)
{
    printf("\t%s: Epoch time: %d -  Last changed: %s \n", 
        fileOrDir, epochCTime, asctime(localtime(&epochCTime)));
}

void parseArgv(int argc, char ** argv, int *deleteDirOption, int *verbose)
{
    *deleteDirOption = 0;
    int opt;

    // TO-DO: Add -l option to redirect standard output to a log file
    // This program being called from a script, the standard output
    // can be redirected...
    while (( opt = getopt(argc, argv, "dv")) != -1) {
        switch (opt) {

        case 'd': *deleteDirOption = 1; break;
        case 'v': *verbose = 1; break;

        default: break;

        }
    }
}

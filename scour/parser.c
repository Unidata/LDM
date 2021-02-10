/**
 * This file parses the ingest config file for the Cscour program input
 *
 *  @file:  parser.c
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

/*
# Configuration file for "scour" utility, to delete all files older than a
# specified number of days from specified directories and all their
# subdirectories.  Scour should be invoked periodically by cron(8).
#
# Each line consists of a directory, a retention time (in days), and
# (optionally) a shell filename pattern for files to be deleted.  If no
# filename pattern is specified, "*" representing all files not beginning with
# "." is assumed.  The syntax "~user" is understood.  Non-absolute pathnames
# are relative to the directory `regutil regpath{PQACT_DATADIR_PATH}`.
#
# A hash in column one indicates a comment line.
# directory			  Days-old		Optional-filename-pattern
#					(days-HHMMSS)
#dir1                   2
#dir2                   2       		*.foo
#~ldm/var/logs          1       		*.stats
#dir3                   2-113055       	*.foo
#dir3                   90-1130       	*.boo
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
#include <regex.h> 
#include <time.h>
#include <pwd.h>
#include <limits.h>


#include "parser.h"

IngestEntry_t* head = NULL;


extern int verbose;
extern char *ingestFilename;
extern int traverseIngestList(IngestEntry_t *);

static int isRegularFile(const char *path)
{
    struct stat path_stat;

    if (stat(path, &path_stat) == -1)
    {
        printf("\tIngest file (\"%s\") does not exist.\n", path);
        return 0;
    }

    return S_ISREG(path_stat.st_mode);
}

void parseArgv(int argc, char ** argv, int *deleteDirOption, int *verbose)
{
    *deleteDirOption = 0;
    int opt;
    int optionsCounter = 0;
    // TO-DO: 
    //  1. Add -l option to redirect standard output to a log file
    // This program being called from a script, the standard output
    // can be redirected...
    //  2. Add option argument to verbose mode to set the level of verbosity
    //  3. Add usage()
    while (( opt = getopt(argc, argv, OPTSTR)) != -1) {
        switch (opt) {

        case 'd': *deleteDirOption = 1; optionsCounter++; break;
        case 'v': *verbose = 1; optionsCounter++; break;
            
        default: abort();

        }
    }
    optionsCounter++;
    
    if( argc - optionsCounter <1)	usage();

    ingestFilename = argv[optind];

 	// check if exists:
 	if( !isRegularFile( ingestFilename )  ) 
 	{
    	// file doesn't exist
    	printf(" Scour Configuration file (%s) does not exist (or not a text file)! Bailing out...\n", 
    		ingestFilename);
    	exit(EXIT_FAILURE);
	}
}

void usage() 
{
	fprintf(stderr, USAGE_FMT, PROGRAM_NAME);
	exit(EXIT_FAILURE);
}

static int getCurrentDir(char *currentDir) {
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != NULL)
	{
		strcpy(currentDir, cwd);
		return 0;
	}
	else
	{
		perror("getcwd() error");
		return -11;
	}
}

/*
	Code to read a file of NON-ALLOWED directory paths into an array
	which is used to skip processing these directories
*/
int readNotAllowedList(char (*list)[STRING_SIZE])
{
    FILE *fp = NULL;
	char notAllowedDirsFilename[PATH_MAX]=NOT_ALLOWED_DIR_PATHS_FILE;
	char currentWorkDir[PATH_MAX];
	if( getCurrentDir(currentWorkDir) == -1)
	{
		fprintf(stderr, "parser::readNotAllowedList(): getcwd() failed: %s\n", strerror(errno));
        return -1;
	}
	//sprintf(notAllowedDirsFilename, "%s/%s", currentWorkDir, NOT_ALLOWED_DIR_PATHS_FILE);

    if((fp = fopen(notAllowedDirsFilename, "r")) == NULL)
    {
        fprintf(stderr, "parser::readNotAllowedList(): fopen(\"%s\") failed: %s\n",
            notAllowedDirsFilename, strerror(errno));
        return -1;
    }

    int i=0;
    while((fscanf(fp,"%s", list[i])) !=EOF) //scanf and check EOF
    {
		if(list[i][0] == '#' || list[i][0] == '\n' ) continue;

 		// printf("list[%i] is '%s'\n", i, list[i]);

    	/*  OR, with pointer
		if((plist + i)[0] == '#' || (plist + i)[0] == '\n' ) continue;
        printf("list[%i] is '%s'\n", i, plist + i);
		*/
        i++;
    }
	return i;
}

IngestEntry_t *parseConfig(int *directoriesCounter)
{
	
	//const char* ingestFilename = SCOUR_INGEST_FILENAME;
    char rejectedDirPathsList[MAX_NOT_ALLOWED_DIRPATHS][STRING_SIZE];

	int notAllowedCounter = readNotAllowedList(rejectedDirPathsList);
	//if(notAllowedCounter <= 0) return NULL;

    FILE *fp = NULL;	
    IngestEntry_t* node= NULL;
    int entryCounter = 0;

	char *line = NULL;
	char dirName[PATH_MAX] = "";
	char pattern[PATTERN_SIZE] = "";
	char daysOld[DAYS_OLD_SIZE] = "";
	char delim[] = " \t\n"; // space, tab and new line
    size_t len = 0;

    if((fp = fopen(ingestFilename, "r")) == NULL)
    {
        fprintf(stderr, "fopen(\"%s\") failed: %s\n",
            ingestFilename, strerror(errno));
        return NULL;
    }

    while ((getline(&line, &len, fp)) != -1) {
		
		if(line[0] == '#' || line[0] == '\n' ) continue;
    
        int itemsCounted=0;
		char *ptr = strtok(line, delim);	
		while(ptr != NULL)
		{

			if(strlen(ptr) > PATH_MAX) {
				verbose && printf("ERROR: %s is TOO long (%zu) !\n", ptr, strlen(ptr));	
				ptr="";
			}

			switch(itemsCounted)
			{
				case 0: strcpy(dirName, ptr);
						break;

				case 1: strcpy(daysOld, ptr);
						break;

				case 2: strcpy(pattern, ptr);
						break;
			}

			ptr = strtok(NULL, delim);
			itemsCounted++;
		}
     
     	// validate dirName path
     	if( vetThisDirectoryPath(dirName, rejectedDirPathsList, notAllowedCounter) )
		{
			verbose && printf("\t(-) Directory '%s' does not exist (or is invalid.) Skipping...\n\n", 
				dirName);
			continue;
		} 

		if(itemsCounted == 2) newEntryNode(dirName, daysOld, ALL_FILES);
		if(itemsCounted == 3) newEntryNode(dirName, daysOld, pattern);
		
		entryCounter++;
    }

    *directoriesCounter = entryCounter;
    fclose(fp);
    if (line)
        free(line);
    //traverseIngestList(head);

    return head;
}

static int notExistAndAccessible(char *dirPath)
{

    DIR *dir = opendir(dirPath);
    if(!dir)
    {
    	fprintf(stderr, "Cscour: failed to open directory %s: %s\n",
            dirPath, strerror(errno));
    	return -1;
    }

    closedir(dir);
    return 0;
}

int isSameAsLoginDirectory(char * dirName)
{
	char *lgnHomeDir;
	if ((lgnHomeDir = loginHomeDir(NULL)) == NULL) return 0;

	return strcmp(dirName, lgnHomeDir);
}

int vetThisDirectoryPath(char * dirName, char (*list)[STRING_SIZE], 
		int notAllowedCounter) 
{

	verbose && printf("\tparser(): validating directory: %s\n", dirName);

	// 1. check if dirName is in the list. If not continue the vetting process
	if( isNotAllowed(dirName, list, notAllowedCounter) ) return -1;

	// 2. check if it starts with tilda and vet the expanded path
	//	  return the expanded path for subsequent vetting below
	char pathName[PATH_MAX];
	if( (startsWithTilda(dirName, pathName)) == NULL ) return -1;
	else {
		strcpy(dirName, pathName);
		verbose && printf("\tparser(): tilda expanded directory:'%s'\n", dirName);
	}

	// 3. check if dir is /home/<userName> and compare with getLogin() --> /home/<userName>
	if( !isSameAsLoginDirectory(dirName) ) return -1;

	// 4. check that the directory is a valid one
    if( notExistAndAccessible(dirName) ) return -1;

	return 0;
}


int isNotAllowed(char * dirName, char (*list)[STRING_SIZE], int notAllowedCounter)
{
	// this can happen if parser found a too long dir path: set it to empty string
	if(dirName == NULL || strlen(dirName) == 0) return -1; 

	int i;
	for(i=0; i<notAllowedCounter; i++) {
		if( strcmp(dirName, list[i]) == 0) {
			verbose && printf("isNotAllowed: path %s is NOT allowed!\n", dirName);
			return -1;
		}
	}
	return 0;
}

// insert a node at the first location in the list
void newEntryNode(char *dir, char *daysOld, char *pattern)
{
    // Allocate a new node in the heap and set its data
    IngestEntry_t *tmp = (IngestEntry_t*) malloc(sizeof(IngestEntry_t));
    
    // convert user's daysOld to Epoch time
    int daysOldInEpoch = convertDaysOldToEpoch(daysOld);
    if(daysOldInEpoch == -1) return;

    // populate node
    strcpy(tmp->dir, dir);
    strcpy(tmp->daysOld, daysOld);
    tmp->daysOldInEpoch = daysOldInEpoch;
    strcpy(tmp->pattern, pattern);

	//point it to old first node
	tmp->nextEntry = head;
	head = tmp;
}

int traverseIngestList(IngestEntry_t *listhead)
{

	IngestEntry_t *tmp = listhead;
	verbose && printf("\n\tparser: Traversing the list of scour items from configuration file: \n");
	if(tmp == NULL) {
		verbose && printf("\n\tEMPTY LIST! \n");
		exit(-1);
	}

   //start from the beginning
   while(tmp != NULL) {
      verbose && printf("\t%s \t %s (%d) \t %s\n",tmp->dir, tmp->daysOld, tmp->daysOldInEpoch, tmp->pattern);
      tmp = tmp->nextEntry;
   }
	
   verbose && printf("\n");

   return 1;

}
// ===============================================================================
//
// Code to parse a regex on daysOld to convert it to Epoch time. 
// It returns the Epoch time of the daysOld string.
// Allowed formats are:
// - days
// - days-HH
// - days-HHMM
// - days-HHMMSS
// It will be used to compare the Epoch mtime of a file to determine if it is 
// a candidate for deletion.
// 
// ===============================================================================

int nowInEpoch()
{
    time_t today, todayEpoch;
	time(&today);
	struct tm *tm_today = localtime(&today);
	todayEpoch = mktime(tm_today);
  
	return todayEpoch;
}

int regexOps(char *pattern, char *daysOldItem, int groupingNumber)
{
	int daysEtcInSeconds=0, days=0, hours=0, minutes=0, seconds=0;

	int todayEpoch = nowInEpoch();
	regex_t regex;
	regmatch_t group[5];
	char *result;

	int status;
	status = regcomp(&regex, pattern, REG_EXTENDED);
	status = regexec(&regex, daysOldItem , groupingNumber, group, 0);
	if (status == 0) 
	{
		result = (char*)malloc(group[1].rm_eo - group[1].rm_so);
		strncpy(result, &daysOldItem[group[1].rm_so], group[1].rm_eo - group[1].rm_so);
		days = atoi(result);
		if(days > DAYS_SINCE_1994) {
			verbose && printf("Too many days back: %d\n", days);
			return -1;
		}

		result = (char*)malloc(group[2].rm_eo - group[2].rm_so);
		strncpy(result, &daysOldItem[group[2].rm_so], group[2].rm_eo - group[2].rm_so);
		hours = atoi(result);

		// 
		switch(groupingNumber)
		{
			case 3: //days_HH
				
				daysEtcInSeconds =  days * DAY_SECONDS + hours * HOUR_SECONDS; 
				verbose && printf("\t(+) daysOld: %d -- hours: %d  (epoch: %d)\n\n", 
					days, hours, todayEpoch - daysEtcInSeconds);
				
				break;

			case 4: // days_HHMM

				result = (char*)malloc(group[3].rm_eo - group[3].rm_so);
				strncpy(result, &daysOldItem[group[3].rm_so], group[3].rm_eo - group[3].rm_so);
				minutes = atoi(result);
		
				daysEtcInSeconds =  days * DAY_SECONDS + hours * HOUR_SECONDS + minutes * MINUTE_SECONDS; 
				verbose && printf("\t(+) daysOld: %d -- hours: %d -- minutes: %d (epoch: %d)\n\n", 
					days, hours, minutes, todayEpoch - daysEtcInSeconds);
		
				break;

			case 5: // days_HHMMSS

				result = (char*)malloc(group[3].rm_eo - group[3].rm_so);
				strncpy(result, &daysOldItem[group[3].rm_so], group[3].rm_eo - group[3].rm_so);
				minutes = atoi(result);			

				result = (char*)malloc(group[4].rm_eo - group[4].rm_so);
				strncpy(result, &daysOldItem[group[4].rm_so], group[4].rm_eo - group[4].rm_so);
				seconds = atoi(result);

				daysEtcInSeconds =  days * DAY_SECONDS + hours * HOUR_SECONDS + minutes * MINUTE_SECONDS + seconds; 
				verbose && printf("\t(+) daysOld: %d -- hours: %d -- minutes: %d -- seconds %d (epoch: %d)\n\n", 
					days, hours, minutes, seconds, todayEpoch - daysEtcInSeconds);

				break;

			default: break;
		}
		free(result);
		regfree(&regex);

		return todayEpoch - daysEtcInSeconds;
	} 
	return -1;
}


/*
Examples of daysOld:
		"1",			--> 1 day
		"2-0630",		--> 2 days + 6 hours + 30 minutes
		"3-073050",		--> 3 days + 7 hours + 30 minutes + 50 seconds
		"3-",			--> error
		"233-0",		--> error
		"33-11",		--> 33 days + 11 hours
		"9000-07",
		"444"
		"0-0930"		--> 0 days + 9 hours + 30 minutes 
*/

int convertDaysOldToEpoch(char *daysOldItem)
{
	regex_t regex;
	regmatch_t group[5];
	int status;
	int todayEpoch = nowInEpoch();

	// days_only
	char *daysOnlyPattern="^[0-9]+$";
	status = regcomp(&regex, daysOnlyPattern, REG_EXTENDED);
	status = regexec(&regex, daysOldItem , 0, NULL, 0);  // no grouping needed
	regfree(&regex);

	if (status == 0) 
	{
		int daysOnlyInSeconds = atoi(daysOldItem) * DAY_SECONDS;
		verbose && printf("\t(+) daysOld: %s (epoch: %d)\n\n", daysOldItem, todayEpoch - daysOnlyInSeconds);
		return todayEpoch - daysOnlyInSeconds;
	} 

	//days-HH

	char *daysHHPattern="^([0-9]+)[-]([0-9]{2})$";
	int res1 = regexOps(daysHHPattern, daysOldItem, 3);

	//days-HHMM
	char *daysHHMMPattern="^([0-9]+)[-]([0-9]{2})([0-9]{2})$";
	int res2 = regexOps(daysHHMMPattern, daysOldItem, 4);
	
	//days-HHMMSS
	char *daysHHMMSSPattern="^([0-9]+)[-]([0-9]{2})([0-9]{2})([0-9]{2})";
	int res3 = regexOps(daysHHMMSSPattern, daysOldItem, 5);
	

	return res1 >= 0? res1 : res2 >= 0? res2 : res3 >= 0? res3 : -1;
}

/*
	Code to examines the directory items:
	- checks for tilda (~) in the directory name and expands it according
	  to the login name returned by getlogin() when not provided as ~ldm. 
	  If ~ldm then ldm username will be used in getpwnam_r(1)
	- checks for NOT ALLOWED directory paths (e.g. /, /var, /home/ldm, ) 
	  and skips storing them in the list
*/



char * loginHomeDir(char *providedLgn)
{
	char *lgn;
	struct passwd *pw;

	if(providedLgn != NULL) lgn = providedLgn;
	else
		if ((lgn = getlogin()) == NULL) {
		    fprintf(stderr, "\"getlogin()\" failed.\n"); 
		    return NULL;
		}

    long int initlen = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t   len;
    if (initlen == -1) {
        /* Default initial length. */
        len = 1024;
    }
    else {
        len = (size_t) initlen;
    }
    struct passwd  result;
    struct passwd* resultp;
    char*          buffer = malloc(len);
    int            status;
    if (buffer == NULL) {
        perror("Couldn't allocate buffer");
        return NULL;
    }
    else {
        int e;
        e = getpwnam_r(lgn, &result, buffer, len, &resultp);
        if (e) {
            perror("getpwnam_r() failure");
            free(buffer);
            return NULL;
        }
        if (resultp == NULL) {
            verbose && fprintf(stderr, "\tUser \"%s\"  does not exist on this system.\n", lgn);
            free(buffer);
            return NULL;
        }
        else {
            // fprintf(stderr, "Found user: %s\n", result.pw_name);
            //strcpy(path, result.pw_name);
            free(buffer);
            return(result.pw_dir);
        }
    }
    free(buffer);
    return NULL;
}

char *substring(char *string, int position, int length)
{
   char *p;
   int c;
 
   p = malloc(length+1);
   
   if (p == NULL)
   {
		fprintf(stderr, "parser(): malloc(\"%d\") failed: %s\n",
        	length +1, strerror(errno));
    	return NULL;
   }
 
   for (c = 0; c < length; c++)
   {
      *(p+c) = *(string+position-1);      
      string++;  
   }
 
   *(p+c) = '\0';
 
   return p;
}

char *startsWithTilda(char *dirPath, char *expandedDirName)
{
    // case ~ldm, expands to ldm's $HOME/ but NOT ALLOWED
    // case ~/ldm, return ldm's $HOME/ldm
    // reject ~ and ~/  only
    // no regex

	// "~"					// NOT 	ALLOWED
	// "~ldm"				// NOT 	ALLOWED
	// "~/"					// NOT 	ALLOWED
	// "~miles/etna/hight"	//		ALLOWED
	// "~/vesuvius"			//		ALLOWED
	// "~ldm/precip"		//  	ALLOWED if ldm is a user

	char subDirPath[PATH_MAX];

    int dirPathLength = strlen(dirPath);	// length already checked in parser

    char *tildaPath = strchr(dirPath, '~');
    int tildaPosition = tildaPath == NULL ? -1 : tildaPath - dirPath;

    if( tildaPosition == -1 || tildaPosition >0) return NULL; // no tilda to expand

    char *tildaRootPath = strchr(dirPath, '/');
    int tildaRootPosition = tildaRootPath == NULL ? -1 : tildaRootPath - dirPath;

	
 	// patterns that are NOT allowed:
	//----------------------------
	
 	// path is ~ only  
 	if(dirPathLength == 1) return NULL;
 
	// path is ~/   only   	
 	if(tildaRootPosition == 1 && dirPathLength == 2) return NULL;

 	// path is ~ldm only 	
	if(tildaRootPosition == -1 && dirPathLength > 1) return NULL; 
 	

 	// patterns that are allowed:
	//----------------------------
	
 	// path is ~ldm/titi ==> $LDM_HOME/titi
 	if(tildaRootPosition > 1 && dirPathLength > tildaRootPosition) 
 	{		
	 	// Expand ldm to LDM_HOME
 		char *providedLgn = substring(dirPath, 2, tildaRootPosition - 1);
 		char *currentHomeDir = loginHomeDir(providedLgn);

 		if(currentHomeDir == NULL) 
 		{
	        verbose && fprintf(stderr, "loginHomeDir() failed:  getpwnam() or getLogin() failed.\n");
 			return NULL;
 		}

 		// Check the length of the expanded login: bail out if new path is too long
 		if(strlen(currentHomeDir) + dirPathLength > PATH_MAX) return NULL;
 		
 		strcpy(subDirPath, currentHomeDir);
 		strcat(subDirPath, dirPath + tildaRootPosition);
 		
 		char *tmp = (char*) malloc((strlen(subDirPath)+1)*sizeof(char));
 		if( tmp == NULL )
 		{
	        fprintf(stderr, "startsWithTilda: malloc() failed: %s\n",  strerror(errno));
 			return NULL;
 		}
 		strcpy(tmp, subDirPath);
 		
 		// return this new dirPath
 		strcpy(expandedDirName, tmp);

 		return expandedDirName;
	}


 	// path is ~/tata ==> $LOGN_HOME/tata
	if(tildaRootPosition == 1 && dirPathLength > tildaRootPosition) 
 	{
 		char *homeDir = loginHomeDir(NULL);
 		if(homeDir == NULL) return NULL;


 		strcpy(subDirPath, homeDir);
 		strcat(subDirPath, dirPath + 1);
 		char *tmp = (char*) malloc((strlen(subDirPath)+1)*sizeof(char));
 		if( tmp == NULL) return NULL;
 		strcpy(tmp, subDirPath);
 		// return this new dirPath
 		//expandedDirName = tmp;
 		strcpy(expandedDirName, tmp);

 		return expandedDirName;
	}

	return "";
}

int xstrcmp(char *str1, char * str2)
{
	char strArray1[80];
	char strArray2[80];
	
	strcpy(strArray1, str1);
	strcpy(strArray2, str2);
	
	return strcmp(strArray1, strArray2);
	
}

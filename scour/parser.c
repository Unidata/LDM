/**
 * This file parses the scour configuration-file for the scour(1) program input
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

#include "config.h"

#include <stdio.h>
#include <stdbool.h>
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
#include <libgen.h>

#include "mygetline.h"
#include "parser.h"
#include "log.h"
#include "globals.h"
#include "registry.h"

/**
 * Parses the scour configuration file and builds a linked list of only
 * vetted dictories
 *
 * @param[out]  directoriesCounter   number of valid directory paths
 * @param[out]  listHead 		     list of valid directory paths
 * @retval      0                    all went well 
 * @retval      -1                   Fatal system error occurred
 */
int
parseConfig(int *directoriesCounter, IngestEntry_t** listHead, char *scourConfPath)
{
    FILE *fp = NULL;	
    int entryCounter = 0;

	char *line = NULL;
	char dirName[PATH_MAX] = "";
	char pattern[PATTERN_SIZE] = "";
	char daysOld[DAYS_OLD_SIZE] = "";
	char delim[] = " \t\n"; // space, tab and new line
    size_t len = 0;

    if((fp = fopen(scourConfPath, "r")) == NULL)
    {
        log_add_syserr("fopen(\"%s\") failed", scourConfPath);
        return -1;
    }

    // The getline(3) function isn't part of _XOPEN_SOURCE=600
    while ((mygetline(&line, &len, fp)) != -1) {
		
		if(line[0] == '#' || line[0] == '\n' ) continue;
    
        int itemsCounted=0;
		char *ptr = strtok(line, delim);	
		while(ptr != NULL)
		{

			if(strlen(ptr) > PATH_MAX) {
				log_add("%s in config file is TOO long (%zu) !", ptr, strlen(ptr));
				log_flush_warning();

				continue;
			}

			switch(itemsCounted)
			{
				case 0: strcpy(dirName, ptr);
						break;

				case 1: strcpy(daysOld, ptr);
						break;

				case 2: strncpy(pattern, ptr, sizeof(pattern));
				        pattern[sizeof(pattern)-1] = 0;
						break;
			}

			ptr = strtok(NULL, delim);
			itemsCounted++;
		}
     
     	// validate dirName path
     	if( vetThisDirectoryPath(dirName) == -1 )
        {
            log_add("(-) Directory '%s' (in scour config) does not exist or is invalid. Skipping it...", dirName);
            log_flush_info();

            continue;
        }

        if(itemsCounted == 2) {
            if (!newEntryNode(listHead, dirName, daysOld, ALL_FILES)) {
                (void)fclose(fp);
                return -1; // DANGER! Assumes list will be freed by termination
            }
        }
        if(itemsCounted == 3) {
            if (!newEntryNode(listHead, dirName, daysOld, pattern)) {
                (void)fclose(fp);
                return -1; // DANGER! Assumes list will be freed by termination
            }
        }

        entryCounter++;
    }

    *directoriesCounter = entryCounter;
    fclose(fp);
    if (line)
        free(line);
    return 0;
}

bool 
isSameAsLoginDirectory(char * dirName)
{
	char *lgnHomeDir;
	if ((lgnHomeDir = loginHomeDir(NULL)) == NULL) 
	{
		log_add("Could not determine login HOME");
		log_flush_warning();
		return true;
	}

	int res = strcmp(dirName, lgnHomeDir);
	if( res == 0)
	{
		log_add("dirName \"%s\" is the same as login name \"%s\"",
			dirName, lgnHomeDir);
		log_flush_warning();
		return true;
	}
	return false;
}

static char *
substring(char *string, int position, int length)
{
   char *p;
   int c;

   p = malloc(length+1);

   if (p == NULL)
   {
		log_add("malloc(\"%d\") failed: %s",
        	length +1, strerror(errno));
		log_flush_error();

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

/**
 * Checks the directory path name present in the scour configuration file
 * to validate the entries there
 *
 * @param[in]  dirPath 		 	directory pathname read from the scour config file 
 * @param[out] expandedDirName	validated directory
 * @retval     0             	directory name is valid
 * @retval     -1            	directory name is not valid
 */
static int 
startsWithTilda(char *dirPath, char *expandedDirName)
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

    if( tildaPosition == -1 || tildaPosition >0)
    {
    	strcpy(expandedDirName, dirPath);
    	return 0; // no tilda to expand
    }

    char *tildaRootPath = strchr(dirPath, '/');
    int tildaRootPosition = tildaRootPath == NULL ? -1 : tildaRootPath - dirPath;


 	// patterns that are NOT allowed:
	//----------------------------

 	// path is ~ only
 	if(dirPathLength == 1) 
 	{
 		log_add("path ~ (tilda only) NOT allowed.");
 		return -1;
 	}
	// path is ~/   only
 	if(tildaRootPosition == 1 && dirPathLength == 2)
 	{
 		log_add("path ~/ (only) NOT allowed.");
 		return -1;
 	}

 	// path is ~ldm only
	if(tildaRootPosition == -1 && dirPathLength > 1)
	{
 		log_add("path ~/<loginName> NOT allowed.");
 		return -1;
 	}

 	// patterns that are allowed:
	//----------------------------

 	// path is ~ldm/titi ==> $LDM_HOME/titi
 	if(tildaRootPosition > 1 && dirPathLength > tildaRootPosition)
 	{
	 	// Expand ldm to LDM_HOME
 		char *providedLgn = substring(dirPath, 2, tildaRootPosition - 1);
 		char *currentHomeDir = loginHomeDir(providedLgn);
 		free(providedLgn);

 		if(currentHomeDir == NULL)
 		{
			log_add("failed:  getpwnam() or getLogin() failed.");
 			return -1;
 		}

 		// Check the length of the expanded login: bail out if new path is too long
 		if(strlen(currentHomeDir) + dirPathLength >= PATH_MAX) return -1;

 		strcpy(subDirPath, currentHomeDir);
 		strcat(subDirPath, dirPath + tildaRootPosition);

 		char *tmp = (char*) malloc((strlen(subDirPath)+1)*sizeof(char));
 		if( tmp == NULL )
 		{
			log_add("malloc() failed: %s",  strerror(errno));
 			return -1;
 		}
 		strcpy(tmp, subDirPath);

 		// return this new dirPath
 		strcpy(expandedDirName, tmp);
 		free(tmp);

 		return 0;
	}

 	// path is ~/tata expand to $LOGIN_HOME/tata
	if(tildaRootPosition == 1 && dirPathLength > tildaRootPosition)
 	{
 		char *homeDir = loginHomeDir(NULL);
 		if(homeDir == NULL) return -1;

 		strcpy(subDirPath, homeDir);
 		strcat(subDirPath, dirPath + 1);
 		char *tmp = (char*) malloc((strlen(subDirPath)+1)*sizeof(char));
 		if( tmp == NULL) return -1;

 		strcpy(tmp, subDirPath);

 		// return this new dirPath
 		strcpy(expandedDirName, tmp);
 		free(tmp);

 		return 0;
	}
	return 0;
}
    
    
    
/**
 * Checks the directory path name present in the scour configuration file
 * to validate the entries there, as far as existence of directory, non-exclusion
 *
 * @param[in]  dirName             directory pathname read from the scour config file 
 * @param[in]  excludedDirsList    list of directory paths of directory to exclude 
 *                                 from scouring
 * @param[in]  excludedDirsCounter previously computed number of excluded directories 
 *
 * @retval     0             	directory name is valid
 * @retval     -1            	directory name is not valid
 */
static int 
vetThisDirectoryPath(char * dirName) 
{
	
	// 1. check if it starts with tilda and vet the expanded path
	//	  return the expanded path for subsequent vetting below
	char pathName[PATH_MAX];
	int tildaFlag = startsWithTilda(dirName, pathName);
	if( tildaFlag == -1) 
	{
		return -1;
	}

	// tilda was found in path and expanded
	strcpy(dirName, pathName);
		
	// 2. check that the directory is a valid one
    if( isNotAccessible(dirName) ) return -1;
	
	// 3. check if dir is /home/<userName> and compare with getLogin() --> /home/<userName>
	//    error if same (dirName should not be /home/<user>)
	if( isSameAsLoginDirectory(dirName) ) return -1;

	return 0;
}

bool
isNotAccessible(char *dirPath) 
{
    int fd = open(dirPath, O_RDONLY);
    if(fd == -1) 
        return true;
    close(fd);
    return false;
}

/**
 * Inserts a node at the first location in the list.
 *
 * @retval `true`   Success. Node created and added if entry is valid
 * @retval `false`  System failure. `log_add()` called.
 */
bool
newEntryNode(IngestEntry_t **listHead, char *dir, char *daysOld, char *pattern)
{
    bool success = false;

    // Allocate a new node in the heap and set its data
    IngestEntry_t *tmp = (IngestEntry_t*) malloc(sizeof(IngestEntry_t));
    if (tmp == NULL) {
        log_add_syserr("Couldn't allocate configuration entry");
    }
    else {
        // convert user's daysOld to Epoch time
        time_t daysOldInEpoch = convertDaysOldToEpoch(daysOld);
        if(daysOldInEpoch == -1) {
            log_add("Couldn't convert days-old parameter: \"%s\"", daysOld);
            log_add("This entry will be ignored");
            log_flush_warning();
            free(tmp);
        }
        else {
            // populate node
            strncpy(tmp->dir, dir, sizeof(tmp->dir))[sizeof(tmp->dir)-1] = 0;
            strncpy(tmp->daysOld, daysOld, sizeof(tmp->daysOld));
            tmp->daysOld[sizeof(tmp->daysOld)-1] = 0;
            tmp->daysOldInEpoch = daysOldInEpoch;
            strncpy(tmp->pattern, pattern, sizeof(tmp->pattern));
            tmp->pattern[sizeof(tmp->pattern)-1] = 0;

            //point it to old first node
            tmp->nextEntry = *listHead; //head;
            *listHead = tmp;
        }
        success = true;
    } // 'tmp` allocated

    return success;
}

time_t
nowInEpoch()
{
    time_t today, todayEpoch;
	time(&today);
	struct tm *tm_today = localtime(&today);
	todayEpoch = mktime(tm_today);
  
	return todayEpoch;
}

/**
 * Converts daysOld specified in scour configuration-file from a string representation
 * into their Epoch time (seconds) equivalent
 * @param[in]  dirName             directory pathname read from the scour config file 
 * @param[in]  excludedDirsList    list of directory paths of directory to exclude 
 *                                 from scouring
 * @param[in]  nmatch              Maximum number of substring matches
 *
 * @retval     >0             	   daysOld in seconds
 * @retval     -1            	   Failure. `log_add()` called.
 */
time_t
regexOps(char *pattern, char *daysOldItem, int nmatch)
{
	// Allowed formats are:
    // - -HH
    // - -HH:MM
	// - days
	// - days-HH
	// - days-HH:MM

	/*
	Examples of daysOld:
			"1",			--> 1 day
			"2-06:30",		--> 2 days + 6 hours + 30 minutes
			"3-",			--> error
			"233-0",		--> error
			"-"             --> error
			"-09"           --> 9 hours
			"33-11",		--> 33 days + 11 hours
			"9000-07",      --> Valid. But seriously!
			"444"           --> 444 days
			"0-09:30"		--> 0 days + 9 hours + 30 minutes
	*/

	int        daysEtcInSeconds=0, days=0, hours=0, minutes=0;
	regex_t    regex;
	regmatch_t group[nmatch];
	char*      result;
	int        status = regcomp(&regex, pattern, REG_EXTENDED);
	time_t     epochTime = -1; // Default failure

	if (status) {
	    const size_t nbytes = regerror(status, &regex, NULL, 0);
	    char         errbuf[nbytes];
	    (void)regerror(status, &regex, errbuf, nbytes);
	    log_add(errbuf);
	    log_add("Couldn't compile pattern \"%s\"", pattern);
	}
	else {
        status = regexec(&regex, daysOldItem , nmatch, group, 0);
        if (status == 0) {
            char* end;
            if (group[1].rm_eo - group[1].rm_so > 0) {
                days = strtol(daysOldItem+group[1].rm_so, &end, 0);
                if(days > DAYS_SINCE_1994) {
                    log_add("Too many days back: %d", days);
                    status = -1;
                }
            }

            if (status == 0) {
                if (group[2].rm_eo - group[2].rm_so > 0)
                    hours = strtol(daysOldItem+group[2].rm_so, &end, 0);

                if (group[3].rm_eo - group[3].rm_so > 0)
                    minutes = strtol(daysOldItem+group[3].rm_so, &end, 0);

                epochTime = nowInEpoch() - (days*DAY_SECONDS + hours*HOUR_SECONDS +
                        minutes*MINUTE_SECONDS);
            }

            regfree(&regex);
        } // `regex` allocated
	}

	return epochTime;
}

time_t
convertDaysOldToEpoch(char *daysOldItem)
{
	return regexOps("^([0-9]*)(-([0-9]{2})(:([0-9]{2}))?)?$", daysOldItem, 4);
}


char * 
loginHomeDir(char *providedLgn)
{
    char *lgn;
    struct passwd *pw;

    if(providedLgn != NULL) lgn = providedLgn;
    else if ((lgn = getlogin()) == NULL) {
        log_add("loginHomeDir:getlogin() failed: %s",  strerror(errno));
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
        log_add("loginHomeDir(): Couldn't allocate buffer");
        return NULL;
    }
    else {
        int e;
        e = getpwnam_r(lgn, &result, buffer, len, &resultp);
        if (e) {
            log_add("loginHomeDir:getpwnam_r() failure");
            free(buffer);
            return NULL;
        }
        if (resultp == NULL) {
            log_add("User \"%s\"  does not exist on this system", lgn);
            free(buffer);
            return NULL;
        }
        else {
            // found user
            free(buffer);
            return(result.pw_dir);
        }
    }
}


int 
xstrcmp(char *str1, char * str2)
{
	return strcmp(str1, str2);
}

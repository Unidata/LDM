/**
 * This file parses the ingest config file for the Cscour program input
 *
 *  @file:  parser.h
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
#include <limits.h>

#ifndef PARSER_DOT_H
#define PARSER_DOT_H

#define ALL_FILES		"*"
#define PATTERN_SIZE	40
// daysOld defs
#define DAYS_OLD_SIZE	15
#define DAY_SECONDS 86400
#define HOUR_SECONDS 3600
#define MINUTE_SECONDS 60
#define DAYS_SINCE_1994 9516

#define MAX_NOT_ALLOWED_DIRPATHS 100
#define DIRS_TO_EXCLUDE_FILE "/tmp/scourExcludedDirectories.txt"

typedef struct IngestEntry {

	char dir[PATH_MAX];
	int  daysOldInEpoch;	
	char daysOld[DAYS_OLD_SIZE];
	char pattern[PATTERN_SIZE];	
	struct IngestEntry* nextEntry;
} IngestEntry_t;

void newEntryNode(IngestEntry_t **, char *, char *, char *);
int parseConfig(int *, IngestEntry_t **);
int regexOps(char *, char *, int);
int nowInEpoch();
int convertDaysOldToEpoch(char *);
char * loginHomeDir(char *);
int isExcluded(char *, char (*)[PATH_MAX], int);
int isAccessible(char *) ;
static int vetThisDirectoryPath(char *, char (*)[PATH_MAX], int); 
static void usage(const char *);

#endif	// PARSER_DOT_H

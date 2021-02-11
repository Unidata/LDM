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

#define PATH_SIZE		1024
#define MAX_STRING_SIZE	40
#define ALL_FILES		"*"
#define DIR_SIZE		80
#define PATTERN_SIZE	20
#define DAYS_SIZE		10

// daysOld defs
#define MAX_LINES 8
#define LINE_SIZE 20
#define DAY_SECONDS 86400
#define HOUR_SECONDS 3600
#define MINUTE_SECONDS 60
#define DAYS_SINCE_1994 9516


#define MAX_NOT_ALLOWED_DIRPATHS 100
#define STRING_SIZE		80
#define DAYS_OLD_SIZE	15
#define NOT_ALLOWED_DIR_PATHS_FILE "/tmp/notAllowedDirs.txt"		// to prepend with current dir (cwd)

#define OPTSTR	"dv"
#define USAGE_FMT  "\n\tUsage: \t%s [-v] [-d] <scour_ingest_filename>\n\n"
#define PROGRAM_NAME	"Cscour"
#
typedef struct IngestEntry {

	char dir[PATH_MAX];
	int  daysOldInEpoch;	
	char daysOld[DAYS_OLD_SIZE];
	char pattern[PATTERN_SIZE];	
	struct IngestEntry* nextEntry;
} IngestEntry_t;

void newEntryNode(char *, char *, char *);
int traverseIngestList(IngestEntry_t *);
IngestEntry_t *parseConfig();

int regexOps(char *, char *, int);
int nowInEpoch();
int convertDaysOldToEpoch(char *);

char * loginHomeDir(char *);
int isNotAllowed(char *, char (*)[STRING_SIZE], int);
int vetThisDirectoryPath(char *, char (*)[STRING_SIZE], int); 
int compareWithGetLogin(char *);
void usage();

int verbose;
#endif	// PARSER_DOT_H

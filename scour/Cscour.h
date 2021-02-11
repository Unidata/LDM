/**
 * This file declares the API for mapping from unit systems to their associated
 * pointers for version 2 of the Unidata UDUNITS package.
 *
 *  @file:  Cscour.h
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

#include "parser.h"

#define SECONDS_IN_1DAY 84000
#define PATH_SIZE       1024
#define SCOUR_FILENAME "/.scour"	// ony use with path
#define MAX_THREADS		200
#define IS_DIRECTORY_SYMLINK 1
#define IS_NOT_DIRECTORY_SYMLINK	0

void*	scourFilesAndDirsForThisPath(void* arg);
int     isOlderThan(int, long);
int     isDirectoryEmpty(char*);
int     isRootPath(char *);
int     isAbsolutePath(char *);

int     existsAndIsAccessible(char *); 
void    parseArgv(int, char **, int *, int *);
int     hasDirChanged(char *);
int    	epochPrettyPrinting(char *, time_t);
void 	scourOnePath(char *, int);
void	multiThreadedScour(IngestEntry_t *, int);
int 	checkGlobPatternMatch(char *, char *);
void	callReadLink(char *, char *);
int 	epochOfLastModified(char *, char *);

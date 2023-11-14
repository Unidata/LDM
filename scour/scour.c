/**
 * 
 * scour(1): a Multi-threaded C program that scours faster than the
 *         scour.sh(1) script
 *         
 *  @file:  scour.c
 * @author: Mustapha Iles
 * @author: Steven R. Emmerson
 *
 *    Copyright 2022 University Corporation for Atmospheric Research
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
#include <time.h>
#include <utime.h>
#include <pwd.h>
#include <pthread.h>
#include <libgen.h>
#include <fnmatch.h>
#include <log.h>
#include <unistd.h>

#include "globals.h"
#include "registry.h"
#include "parser.h"
#include "mygetline.h"

#define MAX_THREADS                 200
#define DIRECTORY_IS_A_SYMLINK      1
#define DIRECTORY_IS_NOT_A_SYMLINK	0
#define NON_EXISTENT_DIR            0
#define EMPTY_DIR                   1
#define NON_EMPTY_DIR               2

typedef struct config_items_args {
    
    char    dir[PATH_MAX];
    time_t  daysOldInEpoch;
    char    daysOld[DAYS_OLD_SIZE];
    char    pattern[PATTERN_SIZE]; 
    int     deleteDirsFlag;

    pthread_t threadId;

} ConfigItemsAndDeleteFlag_t;


// scour configuration file
static char scourConfPath[PATH_MAX];

// Pathname of file containing directories to be excluded from scouring
static char excludePath[PATH_MAX]="";
static char excludedDirsList[MAX_EXCLUDED_DIRPATHS][PATH_MAX];
static int  excludedDirsCount = 0;

static int 
isDirectoryEmpty(char* dirname)
{
    int n=0;
    struct dirent *dp;

    DIR *dir = opendir(dirname);
    if( dir == NULL)
    {
        log_add("failed to open directory \"%s\" (%d: %s)", dirname, errno, strerror(errno));
        log_flush_error();
        return NON_EXISTENT_DIR;
    }

    while((dp = readdir(dir))!=NULL && n<4)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;
        n++;
    }
    closedir(dir);

    return n == 0? EMPTY_DIR : NON_EMPTY_DIR;
}

// The lower the epoch time the older the file
static bool
isThisOlderThanThat(time_t thisFileEpoch, time_t thatFileEpoch)
{
    return (thisFileEpoch <= thatFileEpoch)? true : false;
}


/**
 * Reads the symlink target
 *
 * @param[in]  path     symlink path to follow
 * @param[out] target   target the symlink points to
 * @retval     0        no error
 * @retval     !0       error occured
 */
static int 
callReadLink(char *path, char *target)
{
    int status = 0;
    char buf[PATH_MAX];
    size_t len;

    if ((len = readlink(path, buf, sizeof(buf)-1)) != -1)
    {
        buf[len] = '\0';
    }
    else {
        log_add("readlink(\"%s\") failed: %s\n", path);
        status = -1;
    }
    strcpy(target, buf);
    return status;
}

static bool
isThisSymlinkADirectory(char *path)
{
    struct stat sb;
    
    if (lstat(path, &sb) == -1)
    {
        log_info("symlink \"%s\"  is broken! DELETED!", path);
        
        unlink(path);

        return false;
    }

    return S_ISDIR(sb.st_mode)? true : false;
}

// delete the symlink if target file is older than daysOld, 
// so that symlink is not left broken
static int 
removeFileSymlink(char *symlinkPath, char *symlinkedEntry,
                            time_t daysOldInEpoch, char *daysOld)
{
    char symlinkedFileToRemove[PATH_MAX];

    log_add("removeFileSymlink(): (\"%s\") ", symlinkedEntry);
    log_flush_debug();

    struct stat sb;
    if (stat(symlinkedEntry, &sb) == -1)
    {
        log_info("stat(\"%s\") failed. Or already deleted.", symlinkedEntry);

        // and remove the symlink itself too:
        if (remove(symlinkPath)) {
            log_add_syserr("Couldn't remove symbolic link \"%s\"", symlinkPath);
            log_flush_warning();
        }
        //log_flush_error();
        return -1;
    }

    time_t targetedFileEpoch = sb.st_mtime;
    if( isThisOlderThanThat(targetedFileEpoch, daysOldInEpoch) ) {
        if (remove(symlinkedEntry)) {
            log_add_syserr("Couldn't remove file \"%s\"", symlinkedEntry);
            log_flush_warning();
        }
        // and remove the symlink itself too:
        if (remove(symlinkPath)) {
            log_add_syserr("Couldn't remove symbolic link \"%s\"", symlinkPath);
            log_flush_warning();
        }
    }
    return 0;
}


bool
isExcluded(char * dirPath, char (*list)[PATH_MAX])
{
    for(int i=0; i<excludedDirsCount; i++) {
        if( strcmp(dirPath, list[i]) == 0) {
            log_add("Path %s is an excluded directory!", dirPath);
            log_flush_debug();
            return true;
        }
    }
    return false; // not in the to-exclude list
}

/**
 * Traverses a directory tree, depth-first to scour eligible 
 * files/directoies. Starting at config-specified directory
 * entry and recursively down in-depth first
 *
 * @param[in]  basePath           directory at current depth
 * @param[in]  daysOldInEpoch     daysOld (config) in Epoch time
 * @param[in]  pattern            set in config file. e.g. *.txt
 * @param[in]  deleteDirsFlag     allow / disallow empty directory deletion
 * @param[in]  daysOld            daysOld (as set in config  file)
 * @param[in]  symlinkFlag        flag to distinguish a regular directory
 *                                from a symlink directory traversal type
 * @retval     0                  no error
 * @retval     -1                 error occured
 */
static int 
scourFilesAndDirs(char *basePath, time_t daysOldInEpoch,
                      char *pattern,  int deleteDirsFlag,
                      char *daysOld,  int symlinkFlag)
{
    char symlinkedEntry[PATH_MAX];
    
    // Check if basePath is in the list of excluded directories. 
    // basePath is an absolute path. Excluded dirs are expected to be too.
    if(isExcluded(basePath, excludedDirsList))
    {
        log_info("scourFilesAndDirs():  %s is EXLUDED!\n", basePath);
        return 0;
    }


    struct dirent *dp;
    DIR *dir = opendir(basePath);
    // Unable to open directory stream
    if(!dir)
    {
        log_add("failed to open directory \"%s\" (%d: %s)",
                basePath, errno, strerror(errno));
        log_flush_error();
        return -1;
    }
    
    //int dfd = dirfd(dir);

    while ((dp = readdir(dir)) != NULL)
    {
        struct stat sb;        

        // new basePath 'absPath' is either a dir, a file, or a link to follow if it's a directory symlink
        char absPath[PATH_MAX];
        snprintf(absPath, sizeof(absPath), "%s/%s", basePath, dp->d_name);

        
        if(isExcluded(basePath, excludedDirsList))
        {
            log_info("\nscourFilesAndDirs():  %s is EXLUDED!\n", basePath);
            (void)closedir(dir);
            return 0;
        }

        //if (fstatat(dfd, dp->d_name, &sb, AT_SYMLINK_NOFOLLOW) == -1)
        if (lstat(absPath, &sb) == -1)
        {
            log_info("lstat(\"%s\") failed: %s", absPath, strerror(errno));
            (void)closedir(dir);
            return -1;
        }

        time_t currentEntryEpoch = sb.st_mtime;

        switch (sb.st_mode & S_IFMT)
        {
        case S_IFLNK:

            if ( callReadLink(absPath, symlinkedEntry) == -1 )
            {
                log_flush_warning();
                continue;
            }

            if(isThisSymlinkADirectory(absPath))
            {
                log_info("(sl-d) Following symlink: %s (Will not be removed.)\n", symlinkedEntry);

                // recursive call:
                scourFilesAndDirs(symlinkedEntry, daysOldInEpoch, pattern,
                                    deleteDirsFlag, daysOld, DIRECTORY_IS_A_SYMLINK);

                // Directories of a SYMLINK will not get removed: How to ensure that??
            }
            else
            {
                // delete the symlink if target file is older than daysOld, so that symlink 
                // is not left broken. However, currentEntryEpoch should NOT be that of the 
                // symlink but that of the target file pointed to by the slink
                if( !removeFileSymlink(absPath, symlinkedEntry, daysOldInEpoch, daysOld))
                {
                    log_info("(sl-r) %s is a symlinked file and OLDER than %s daysOld (days[-HH[MM]]). DELETED!",
                            symlinkedEntry, daysOld);
                }
            }
            break;

        case S_IFDIR :

            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                continue;

            // depth-first traversal
            scourFilesAndDirs(absPath, daysOldInEpoch, pattern, deleteDirsFlag,
                                daysOld, symlinkFlag);


            // The excluded path is a leaf (no deep dive performed): DO NOT DELETE
            bool isExcludedDirFlag = isExcluded(absPath, excludedDirsList);

            // Directory status: empty | not empty | non-existent (opendir() failed)
            int dirStatus = isDirectoryEmpty(absPath);
            if (dirStatus == NON_EXISTENT_DIR)
            {
                break;
            }
            // Remove if empty and not symlinked, regardless of its age (daysOld)
            if( (dirStatus == EMPTY_DIR) && !symlinkFlag && deleteDirsFlag && !isExcludedDirFlag)
            {
                log_info("Empty directory and NOT a symlink: %s. DELETED!", absPath);
                if(remove(absPath))
                {
                    log_add("directory remove(\"%s\") failed", absPath);
                    log_flush_error();
                    break;
                }
            }
            else 
            {
                if( symlinkFlag )
                    log_info("Directory \"%s\" is a SYMLINK. NOT deleted.", absPath);
                if( isExcludedDirFlag )
                    log_info("Directory \"%s\" is EXCLUDED. NOT deleted.", absPath);
                else
                    log_info("Directory \"%s\" is NOT EMPTY. NOT deleted.", absPath);
            }
            break;

        case S_IFREG :

            // Only examine pattern-matching files and non-.scour files
            // fnmatch returns 0 if match found
            if( fnmatch(pattern, dp->d_name, FNM_PATHNAME)  )
            {
                //log_info("(-) File \"%s\" does NOT match pattern: %s",  dp->d_name, pattern);
                continue;
            }

            //log_add("(+) File \"%s\" matches pattern: %s",  dp->d_name, pattern);
            if ( isThisOlderThanThat(currentEntryEpoch, daysOldInEpoch) )
            {
                if( remove(absPath) )
                {
                    log_add("remove(\"%s\") failed", absPath);
                    log_flush_error();
                }
                else
                {
                    // current file is OLDER than daysOld
                    log_info("(+)File \"%s\" is OLDER than %s (days[-HH[MM]]) - DELETED!", absPath, daysOld);
                }
                // in any case
                continue;
            }

            //log_info("(-) File \"%s\" is NOT older than %s (days[-HH[MM]]) - Skipping it...",
            //            absPath, daysOld);
            break;


        default:
            // It should never get here
            log_add("(?) NOT a regular file, nor a symlink: \"%s\"", absPath);
            log_flush_error();

            break;
        }
        log_flush_info();
    }
    closedir(dir);
    return 0;
}


/**
 * Thread function to initiate the directory scouring for a given 
 * config directory entry running in its own thread
 *
 * @param[in]  oneItemStruct      structure pointer that holds all
 *                                information for a single directory entry
 */
static void* 
scourFilesAndDirsForThisPath(void *oneItemStruct)
{

    ConfigItemsAndDeleteFlag_t currentItem = *(ConfigItemsAndDeleteFlag_t *)
                                                oneItemStruct;

    char*  dirPath          = currentItem.dir;
    char*  daysOld          = currentItem.daysOld;     // <days>[-HH[MM]], eg. 1-1220
    time_t daysOldInEpoch   = currentItem.daysOldInEpoch;     // parsed from <days>[-HH[MM]], eg. 1-1220 to Epoch time
    char*  pattern          = currentItem.pattern;

    int    deleteDirFlag    = currentItem.deleteDirsFlag;

    // free memory of the struct that was allocated in the calling function: multiThreadedScour()
    free((ConfigItemsAndDeleteFlag_t *) oneItemStruct);

    // Check if dirPath is in the list of excluded direectories. 
    // dirtPath is an absolute path. Excluded dirs are expected to be too.
    bool thisDirIsNotExcluded = !isExcluded( dirPath, excludedDirsList);

    // Check directory status for non-existent (opendir() failed)
    int dirStatus = isDirectoryEmpty(dirPath);
    if (dirStatus == NON_EXISTENT_DIR)
    {
        log_add("directory (\"%s\") does not exist (opendir() failed)", dirPath);
        log_flush_error();   
    }

    // - scour candidate files and directories under 'path' - recursively
    // - It ASSUMES that this first entry directory is NOT a symbolic link
    //   (should we consider a starting directory as a symlink?)
    // - delete empty directories if delete option (-d) is set
    thisDirIsNotExcluded && (dirStatus != NON_EXISTENT_DIR) &&
    scourFilesAndDirs(  dirPath, daysOldInEpoch, pattern,
                        deleteDirFlag, daysOld, DIRECTORY_IS_NOT_A_SYMLINK );

    // after bubbling up, remove top directory if empty and if delete option is set


    if( thisDirIsNotExcluded && (dirStatus == EMPTY_DIR) && deleteDirFlag )
    {
        if (remove(dirPath)) {
            log_add_syserr("Couldn't remove directory \"%s\"", dirPath);
            log_flush_warning();
        }
    }

    // TO-DO:  remove dangling Symlinks

    pthread_exit(0);
}


/**
 * This is the thread-creation function. A thread is generated for
 * each entry in the list (listTete), coming from the config directory file
 *
 * @param[in]  listTete           directory at current depth
 * @param[in]  deleteDirsFlag     flag to enable the directory-deletion
 *                                
 * @retval     0                  no error
 * @retval     -1                 error occured
 */
static void 
multiThreadedScour(IngestEntry_t *listTete, int deleteDirsFlag)
{
    if(listTete == NULL)
    {
        log_add("Empty list of directories to scour. Bailing out...");
        log_flush_fatal();
        return; 
    }
    
    log_info("List of validated items sourced in user's configuration file: %s", 
            scourConfPath);
    
    IngestEntry_t *tmp = listTete;
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

        log_info("Processing directory: %s with %s daysOld and pattern: %s",
                tmp->dir, tmp->daysOld, tmp->pattern);
        
        strcpy(items->dir,      tmp->dir);
        items->daysOldInEpoch =  tmp->daysOldInEpoch;
        strcpy(items->daysOld,  tmp->daysOld);
        strcpy(items->pattern,  tmp->pattern);     
        items->deleteDirsFlag = deleteDirsFlag;     // delete or not delete empty directories

        // Create attributes & init
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        int status = pthread_create(&tids[threadsCounter++], &attr,
                    scourFilesAndDirsForThisPath, items);
        if (status) {
            log_add_errno(status, "Couldn't create thread for directory \"%s\"",
                    items->dir);
            log_flush_warning();
        }
        
        tmp = tmp->nextEntry;
    }

    // wait until the thread is done executing
    tmp = listTete;
    int i=0;
    while(tmp != NULL) 
    {
        // Thread ID: wait on this thread
        (void)pthread_join(tids[i++], NULL); // Threads can't deadlock
        log_add("Directory scouring (%s) completed in thread #%d!", tmp->dir, i-1);
        log_flush_info();
  
        tmp = tmp->nextEntry; 
    } 

}

/**
 * Checks if 'path' is a regular file.
 *
 * @param[in]  path          scour config file
 * @retval     0             boolean false: 'path' is NOT a regular file                           
 * @retval     !0            boolean true: 'path' is a regular file
 */
static bool 
isRegularFile(const char *path)
{
    struct stat path_stat;

    if (stat(path, &path_stat) == -1)
    {
        return false;
    }

    return (S_ISREG(path_stat.st_mode)) ? true : false;
}

/**
 * Builds a list of to-be-excluded directory paths (not scoured)
 *
 * @param[in]  pathname     Pathname of file containing pathnames of directories
 *                          to be excluded from scouring
 * @param[out] list         List of directories to be excluded
 * @retval     >=0          Number of directories to be excluded
 * @retval     -1           Error parsing exclusion file
 */
int 
getExcludedDirsList(char (*list)[PATH_MAX], char *pathname)
{
    FILE *fp = NULL;

    if (access(pathname, F_OK)) {
        if (errno == ENOENT)
            // If it doesn't exist, that's not an error: there are no entries
            return 0;
    }
    if((fp = fopen(pathname, "r")) == NULL)
    {
        // If it exists but can't be opened, that's an error
        log_info("Excluded-directory file: fopen(\"%s\") failed: %s. "
                "(Continuing without it.)", pathname,
                strerror(errno));
        return -1;
    }

    char*  lineptr = NULL;
    size_t bufsize = 0;
    int    lineNo = 0;
    for (;;) {
        ssize_t nchar = mygetline(&lineptr, &bufsize, fp);
        if (nchar == 0) {   // EOF
            (void)fclose(fp);
            return lineNo;
        }
        if (nchar == -1) {
            if (ferror(fp)) {
                log_add_syserr("mygetline() failure");
                (void)fclose(fp);
                return -1;
            }
            break; // EOF encountered
        }
        if (lineNo >= MAX_EXCLUDED_DIRPATHS) {
            log_add("Number of entries exceeds limit of %d",
                    MAX_EXCLUDED_DIRPATHS);
            (void)fclose(fp);
            return -1;
        }
        if (lineptr[nchar-1] == '\n') {
            lineptr[nchar-1] = 0;
            --nchar;
        }
        if (nchar >= sizeof(*list)) {
            log_add("Line %d in \"%s\" is too long: \"%s\"", lineNo+1,
                    pathname, lineptr);
            (void)fclose(fp);
            return -1;
        }
        strncpy(list[lineNo++], lineptr, sizeof(*list))[sizeof(*list)-1] = 0;
    }
    (void)fclose(fp);
    free(lineptr);
    return lineNo; // EOF encountered
}

static void
validateScourConfFile(char *argvPath, char *scourConfPath, const size_t size)
{
    log_add("User input argv: %s", argvPath);
    log_add("Default conf-file: %s", scourConfPath);
    log_flush_debug();

    if( argvPath != NULL)
    {
        if(!isRegularFile(argvPath))
        {
            log_add("User-supplied conf-file (%s) is NOT accessible! Bailing out...", argvPath);
            log_flush_error();
            exit(EXIT_FAILURE);
        } 
        log_add("User-supplied scour conf-file: %s", argvPath);    
        log_flush_info();

        (void) memset(scourConfPath, 0, size);
        (void) strncpy(scourConfPath, argvPath, PATH_MAX-1);
    }
    else {
        log_add("Default scour conf-file (%s).", scourConfPath);
        log_flush_info();

        // check if exists: even as a default it may not be valid
        if( !isRegularFile( scourConfPath )  ) 
        {
            log_add("Default conf-file (%s) is NOT accessible! Bailing out...", 
                scourConfPath);
            log_flush_error();
            exit(EXIT_FAILURE);
        }
    }
    log_add("Scour conf-file used: %s", scourConfPath); 
    log_flush_info();
}

/**
 * Logs a usage message.
 *
 * @param[in] progname  Name of program
 * @param[in] level     Logging level
 */
static void 
usage(const char*       progname,
      const log_level_t level)
{
    log_log(level,
"Usage:\n"
"       %s -h \n"
"       %s [-d] [-e <excludes>] [-l dest] [-v|-x] [<config>]\n"
"Where:\n"
" -d            Enable directory deletion.\n"
" -e <excludes> Pathname of file listing directories to be excluded.\n"
"               Default is \"%s\".\n"
" -h            Print this usage() message and exit.\n"
" -l dest       Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `dest`.\n"
"               Default is \"%s\".\n"
" -v            Log messages down to the INFO level\n"
" -x            Log messages down to the DEBUG level\n"
" config        Configuration file.\n"
"               Default is \"%s\".\n",
            progname,
            progname,
            excludePath,
            log_get_destination(),
            scourConfPath);
}

/**
 * Gets the value of an LDM registry parameter as a string or uses a default
 * value.
 *
 * @param[in]  name    Name of parameter
 * @param[out] value   Value of parameter
 * @param[in]  def     Default value for parameter
 * @retval     0       Success. `*value` is set. Caller should free when it's no
 *                     longer needed.
 * @retval     EINVAL  Invalid parameter name. `log_add()` called.
 * @retval     EIO     Backend database error.  "log_add()" called.
 * @retval     ENOMEM  System error.  "log_add()" called.
 */
static int
getRegString(const char*       name,
             char** const      value,
             const char* const def)
{
    int status = reg_getString(name, value);

    if (status == ENOENT) {
        // No such entry => use default
        log_clear();

        char* val = strdup(def);
        if (val == NULL) {
            log_add_syserr("Couldn't duplicate default value \"%s\"", def);
            status = ENOMEM;
        }
        else {
            *value = val;
            status = 0;
        } // Default string duplicated
    }
    return status;
}
/*
 *  Sets global variables: scourConfPath and excludePath
 */
void getRegistryConfValues(char * workingDir)
{
    char* var = NULL;
  char myHomePath[PATH_MAX];
  char myScourPath[PATH_MAX];
  memset((void *)myHomePath, 0, sizeof(myHomePath));
  memset((void *)myScourPath, 0, sizeof(myScourPath));
  strncpy(myHomePath, getenv("HOME"), (sizeof(myHomePath) - 1));
  snprintf(myScourPath, (sizeof(myScourPath) - 1), "%s/etc/scour_excludes.conf", myHomePath);
  if (getRegString(REG_SCOUR_EXCLUDE_PATH, &var, myScourPath) == 0)
        strncpy(excludePath, var, sizeof(excludePath)-1);
    free(var);
 
  memset((void *)myScourPath, 0, sizeof(myScourPath));
  snprintf(myScourPath, (sizeof(myScourPath) - 1), "%s/etc/scour.conf", myHomePath);
    if (getRegString(REG_SCOUR_CONFIG_PATH, &var, myScourPath)) {
        log_add("Couldn't get scour config path for this program");
        log_flush_fatal();
        exit(EXIT_FAILURE);
    }
    strncpy(scourConfPath, var, sizeof(scourConfPath)-1);
    free(var);   

    // Set the current working directory to that of pqact(1) processes 
  memset((void *)myScourPath, 0, sizeof(myScourPath));
  snprintf(myScourPath, (sizeof(myScourPath) - 1), "%s/var/data", myHomePath);
    if (getRegString(REG_PQACT_DATADIR_PATH, &var, myScourPath)) {
        log_add("Couldn't get working directory for this program");
        log_flush_fatal();
        exit(EXIT_FAILURE);
    }
    strcpy(workingDir, var);
    free(var);       

}

static bool
changeDirectory(char* workingDir)
{
    bool success;

    if (chdir(workingDir)) {
        log_add_syserr("Couldn't change working directory to \"%s\"",
                workingDir);
        success = false;
    }
    else {
        log_info("Changed working directory to \"%s\"", workingDir);
        success = true;
    }

    return success;
}

int 
main(int argc, char *argv[])
{
    int  status = 0,
         deleteDirsFlag = 0,
         debugMode = 0,
         ch=0;
    char logFilename[PATH_MAX]="";
    char workingDir[PATH_MAX];    
    const char* const   progname = basename(argv[0]);
    /*
     * Initializes logging. Done first, just in case something happens
     * that needs to be reported.
     */
    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(EXIT_FAILURE);
    }

    // Read macro from the registry. If not there, take the default value
    // from config.h
    (void) getRegistryConfValues(workingDir);

    extern char* optarg;
    extern int   optind;
    extern int   opterr;
    opterr = 0;
    while (( ch = getopt(argc, argv, ":de:hvxl:")) != -1) {

        switch (ch) 
        {
        case 'd':   {
                    deleteDirsFlag = 1;
                    break;
                }
        case 'h':   {
                    log_level_t prevLevel = log_get_level();
                    log_set_level(LOG_LEVEL_INFO);
                    usage(progname, LOG_LEVEL_INFO);
                    log_set_level(prevLevel);
                    exit(EXIT_SUCCESS);
                }
        case 'e':   {
                    (void)strncpy(excludePath, optarg, sizeof(excludePath)-1);
                    break;
                }
        case 'l':   {
                    if (log_set_destination(optarg)) {
                        log_fatal("Couldn't set logging destination to \"%s\"", optarg);
                        usage(progname, LOG_LEVEL_FATAL);
                        exit(EXIT_FAILURE);
                    }
                    strncpy(logFilename, optarg, sizeof(logFilename));
                    logFilename[sizeof(logFilename)-1] = 0;
                    log_info("logfilename: %s", logFilename);
                    break;
                }
        case 'v':  {
                    if (!log_is_enabled_info)
                        log_set_level(LOG_LEVEL_INFO);
                    break;
                }

        case 'x':   {                    
                    if (!log_is_enabled_debug)
                        log_set_level(LOG_LEVEL_DEBUG);
                    break;
                }
        case ':': {
                    log_fatal("Option \"-%c\" requires a positional argument", ch);
                    usage(progname, LOG_LEVEL_FATAL);
                    exit(EXIT_FAILURE);
                }
        default:    {
                    log_fatal("Unknown option: \"%c\"", ch);
                    usage(progname, LOG_LEVEL_FATAL);
                    exit(EXIT_FAILURE);
                }
        }
    }

    if(argc - optind > 1) {
        log_fatal("Too many arguments");
        usage(progname, LOG_LEVEL_FATAL);
        exit(EXIT_FAILURE);
    }
    
    (void) validateScourConfFile(argv[optind], scourConfPath,
            sizeof(scourConfPath));
  
    // build the list of excluded directories
    excludedDirsCount = getExcludedDirsList(excludedDirsList, excludePath);
    if (excludedDirsCount == -1) {
        log_add("Couldn't parse excluded-directories file");
        log_flush_fatal();
        exit(EXIT_FAILURE);
    }

    int validEntriesCounter = 0;
    IngestEntry_t *listHead = NULL;

    // Call config parser
    if( parseConfig(&validEntriesCounter, &listHead, scourConfPath) != 0)
    {
        log_add("Parsing conf-file failed.");
        log_flush_fatal();
        exit(EXIT_FAILURE);
    }
    if( validEntriesCounter == 0 )
    {
        log_add("NO VALID directory entries found.");
        log_flush_warning();
        exit(EXIT_SUCCESS);
    }

    if (!changeDirectory(workingDir)) {
        char buf[PATH_MAX] = {};
        log_add("Relative pathnames in configuration-file will be interpreted "
                "relative to %s", getcwd(buf, sizeof(buf)-1));
        log_flush_warning();
    }

    log_info("Launching %d threads...", validEntriesCounter);
    (void) multiThreadedScour(listHead, deleteDirsFlag);

    log_free();

    exit(EXIT_SUCCESS);
}


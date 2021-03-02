/**
 * 
 * Cscour(1): a Multi-threaded C program that scours faster than the
 *         scour(1) script
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

#define MAX_THREADS		200
#define IS_DIRECTORY_SYMLINK 1
#define IS_NOT_DIRECTORY_SYMLINK	0

typedef struct config_items_args {
    
    char    dir[PATH_MAX];
    int     daysOldInEpoch;
    char    daysOld[DAYS_OLD_SIZE];
    char    pattern[PATTERN_SIZE]; 
    int     deleteDirsFlag;

    pthread_t threadId;

} ConfigItemsAndDeleteFlag_t;


// scour configuration file
static char scourConfPath[PATH_MAX]="/tmp/scourTest.conf";

// Pathname of file containing directories to be excluded from scouring
static char excludePath[PATH_MAX]="";


static bool 
isDirectoryEmpty(char* dirname)
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

    return n == 0? true : false;
}

// The lower the epoch time the older the file
static bool
isThisOlderThanThat(int thisFileEpoch, int thatFileEpoch)
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
isSymlinkDirectory(char *path)
{
    struct stat sb;
    if (stat(path, &sb) == -1)
    {
        log_info("symlink \"%s\"  is broken! Unlinking it...", path);
        
        unlink(path);

        return false;
    }
    return S_ISDIR(sb.st_mode)? true : false;
}

// delete the symlink if target file is older than daysOld, 
// so that symlink is not left broken
static int 
removeFileSymlink(char *symlinkPath, char *symlinkedEntry,
                            int daysOldInEpoch, char *daysOld)
{
    char symlinkedFileToRemove[PATH_MAX];

    struct stat sb;
    if (stat(symlinkedEntry, &sb) == -1)
    {
        log_add("stat(\"%s\") failed", symlinkedEntry);
        log_flush_error();
        return -1;
    }

    int targetedFileEpoch = sb.st_mtime;
    if( isThisOlderThanThat(targetedFileEpoch, daysOldInEpoch) ) {
        remove(symlinkedEntry);
        // and remove the symlink itself too:
        remove(symlinkPath);
    }
    return 0;
}


/**
 * Traverses a directory tree, depth-first to scour eligible 
 * files/directoies. Starting at config-specified directory
 * entry and recursively in-depth first
 *
 * @param[in]  basePath           directory at current depth
 * @param[in]  daysOldInEpoch     daysOld (config) in Epoch time
 * @param[in]  pattern     s
 * @param[in]  deleteDirsFlag     s
 * @param[in]  daysOld            daysOld (as set in config  file)
 * @param[in]  symlinkFlag        flag to distinguish a regular directory
 *                                from a symlink directory traversal type
 * @retval     0                  no error
 * @retval     -1                 error occured
 */
static int 
scourFilesAndDirs(char *basePath, int daysOldInEpoch,
                      char *pattern,  int deleteDirsFlag,
                      char *daysOld,  int symlinkFlag)
{
    char symlinkedEntry[PATH_MAX];
    
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

    int dfd = dirfd(dir);

    while ((dp = readdir(dir)) != NULL)
    {
        struct stat sb;
        if (fstatat(dfd, dp->d_name, &sb, AT_SYMLINK_NOFOLLOW) == -1)
        {
            log_add("fstatat(\"%s/%s\") failed: %s", basePath, dp->d_name, strerror(errno));
            log_flush_error();
            return -1;
        }

        int currentEntryEpoch = sb.st_mtime;

        // new basePath is either a dir, a file, or a link to follow if it's a directory symlimk
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", basePath, dp->d_name);


        switch (sb.st_mode & S_IFMT)
        {
        case S_IFDIR :

            if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                continue;

            // depth-first traversal
            scourFilesAndDirs(path, daysOldInEpoch, pattern, deleteDirsFlag,
                                daysOld, symlinkFlag);

            // Remove if empty and not symlinked, regardless of its age (daysOld)
            if( isDirectoryEmpty(path) && !symlinkFlag && deleteDirsFlag)
            {
                log_info("Empty directory: %s. DELETED!", path);
                if(remove(path))
                {
                    log_add("directory remove(\"%s\") failed", path);
                    log_flush_error();
                    break;
                }

            } else 
            {
                    log_info("Directory \"%s\" is NOT empty. NOT deleted.", path);
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
                if( remove(path) )
                {
                    log_add("remove(\"%s\") failed", path);
                    log_flush_error();
                }
                else
                {
                    // current file is OLDER than daysOld
                    log_info("\n(+)File \"%s\" is OLDER than %s (days[-HHMMSS]) - DELETED!", path, daysOld);
                }
                // in any case
                continue;
            }

            //log_info("(-) File \"%s\" is NOT older than %s (days[-HHMMSS]) - Skipping it...",
            //            path, daysOld);

            break;

        case S_IFLNK:

            if ( callReadLink(path, symlinkedEntry) == -1 )
            {
                log_flush_warning();
                continue;
            }

            if(isSymlinkDirectory(path))
            {
                log_info("(sl-d) Following symlink: %s (Will not be removed.)\n",
                    symlinkedEntry);

                // recursive call:
                scourFilesAndDirs(symlinkedEntry, daysOldInEpoch, pattern,
                                    deleteDirsFlag, daysOld, IS_DIRECTORY_SYMLINK);

                // Directories of a SYMLINK will not get removed.
            }
            else
            {
                // delete the symlink if target file  is older than daysOld, so that symlink is not left broken
                // however, currentEntryEpoch should NOT be that of the symlink but that of the file pointed to by the slink
                if( !removeFileSymlink(path, symlinkedEntry, daysOldInEpoch, daysOld))
                {
                    log_info("(sl-r) %s is a symlinked file and OLDER than %s daysOld (days[-HHMMSS]). DELETED!", 
                            symlinkedEntry, daysOld);
                }
            }
            break;

        default:
            // It should never get here
            log_add("(?) NOT a regular file, nor a symlink: \"%s\"", dp->d_name);
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
    if( isDirectoryEmpty(dirPath) && deleteDirOrNot )
    {
        remove(dirPath);
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
        pthread_create(&tids[threadsCounter++], &attr, 
                    scourFilesAndDirsForThisPath, items);
        
        tmp = tmp->nextEntry;
    }

    // wait until the thread is done executing
    tmp = listTete;
    int i=0;
    while(tmp != NULL) 
    {
        // Thread ID: wait on this thread
        pthread_join(tids[i++], NULL);
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


static void 
usage(const char* progname)
{
    log_add(
"Usage:\n"
"       %s [-v] [-d] [-e exclude_path] [-l dest] [scour_configuration_pathname]\n"
"Where:\n"
"  -d               Enable directory deletion\n"
"  -e exclude_path  Pathname of file listing directories to be excluded. "
                    "Default is \n"
"                   \"%s\".\n"
"  -l dest          Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                   (standard error), or file `dest`. Default is\n"
"                   \"%s\".\n"
"  -v               Log INFO messages\n",
            progname,
            excludePath,
            log_get_default_destination());
    log_flush_error();
}

int 
main(int argc, char *argv[])
{
    int status = 0,
        deleteDirsFlag = 0,
        debugMode = 0,
        ch=0;
    char logFilename[PATH_MAX]="";
    
    int opterr;
    char *optarg;
    /*
     * Initializes logging. Done first, just in case something happens
     * that needs to be reported.
     */
    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(EXIT_FAILURE);
    }

    const char* const   progname = basename(argv[0]);
    char*       var;
    if (reg_getString(REG_SCOUR_EXCLUDE_PATH, &var)) {
        strncpy(excludePath, SCOUR_EXCLUDE_PATH, sizeof(excludePath)-1);
    }
    else {
        strncpy(excludePath, var, sizeof(excludePath)-1);
        free(var);
    }

    opterr = 0;
    while (( ch = getopt(argc, argv, ":de:vxl:")) != -1) {

        switch (ch) {

        case 'd':   {
                    deleteDirsFlag = 1;
                    break;
                }
        case 'e':   {
                    (void)strncpy(excludePath, optarg, sizeof(excludePath)-1);
                    break;
                }
        case 'l':   {
                    if (log_set_destination(optarg)) {
                        log_syserr("Couldn't set logging destination to \"%s\"", optarg);
                        usage(progname);
                    }
                    strcpy(logFilename, optarg);
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
                    log_add("Option \"-%c\" requires a positional argument", ch);                
                    usage(progname);
                    exit(EXIT_SUCCESS);                   
                }
        default:    {
                    log_add("Unknown option: \"%c\"", ch);
                    usage(progname);
                    exit(EXIT_SUCCESS);
                }
        }
    }


    if(argc - optind > 1)  usage(progname);

    if(argv[optind] != NULL)
    {
        // Check configuration file is valid
        if(!isRegularFile(argv[optind]))
        {
            log_add("Scour configuration file (%s) does not exist (or is not a text file)! Bailing out...", 
                argv[optind]);
            log_flush_error();
            exit(EXIT_FAILURE);
        }
        log_add("Optind: %d = argv[%d]: %s", optind, optind, argv[optind]);    
        log_flush_debug();
        (void) strncpy(scourConfPath, argv[optind], sizeof(scourConfPath));
    }
    else {
        log_add("Proceeding with default scour configuration file (%s).", scourConfPath);
        log_flush_warning();
    }

    log_add("Excluded Directories pathname: %s", excludePath); 
    log_add("Scour config file pathname: %s", scourConfPath);
    log_flush_debug();
    
    // check if exists:
    if( !isRegularFile( scourConfPath )  ) 
    {
        // file doesn't exist
        log_add("Scour configuration file (%s) does not exist (or is not a text file)! Bailing out...", 
            scourConfPath);
        log_flush_error();
        exit(EXIT_FAILURE);
    }

    log_info("STARTED...");
    log_info("parsing...");

    // Call config parser
    int validEntriesCounter = 0;
    IngestEntry_t *listHead = NULL;

    if( parseConfig(&validEntriesCounter, &listHead, excludePath, scourConfPath) != 0)
    {
        log_add("parseConfig() failed");
        log_add("parsing complete!");
        log_add("COMPLETED!");
        log_flush_fatal();
        exit(EXIT_FAILURE);
    }

    if( validEntriesCounter == 0 || listHead == NULL)
    {
        log_add("no valid configuration file entries");
        log_add("parsing complete!");
        log_add("COMPLETED!");
        log_flush_warning();
        exit(EXIT_SUCCESS);
    }

    log_info("parsing complete!");
    log_info("Launching %d threads...", validEntriesCounter);

    multiThreadedScour(listHead, deleteDirsFlag);

    log_info("COMPLETED!");
    log_free();

    exit(EXIT_SUCCESS);
}


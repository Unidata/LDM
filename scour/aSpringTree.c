#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <errno.h>
#include <getopt.h>

#define SIZE_OF_FILE     1024
#define NUMBER_OF_FILES  10000
#define NUMBER_OF_DIRS   1000
#define MAX_NUMBER_OF_FILES_PER_DIRECTORY    100000
#define MIN_NUMBER_OF_FILES_PER_DIRECTORY    100

// tree volume
#define DEPTH       4
#define BREADTH     5
#define TXT_EXTENSION    "txt"
#define FOO_EXTENSION    "csv"

#define OPTSTR "is"
#define USAGE_FMT  "\n\tUsage: \t%s [-i <days_old_index>] [-s] <root_directory>\n\n"
#define PROGRAM_NAME     "aSpringTree"

#define DAYS_OLD_ARRAY_LENGTH  5

typedef enum {
     OneK      = 1024,        // 1 K
     TenK      = 10240,       // 10 K
     HundredK  = 102400,      // 100 K
     OneMeg    = 1024000,      // 1 MG
     FiveMegs  = 5120000,       // 5MG
     TenMegs   = 10240000       // 10MG
} FILE_SIZES;

typedef enum {
     OneDay    = 86400,   
     TwoDays   = 172800,
     ThreeDays = 259200,
     FourDays  = 345600,
     FiveDays  = 432000
} DAYS_OLD_IN_SEC;

typedef struct RegularFile {
     char filename[PATH_MAX];
     int fileSize;
     int fileType;    // regular
} RegularFile_t;


typedef struct DirectoryFile {
     char dirName[PATH_MAX];
     int fileType;    // directory
} DirectoryFile_t;


typedef struct SymlinkFile {
     char symlink[PATH_MAX];
     int fileType;    // symlink
} SymlinkFile_t;


int randomFilesCreation(char *, int);


void usage() 
{
     fprintf(stderr, USAGE_FMT, PROGRAM_NAME);
     exit(EXIT_FAILURE);
}


int nowInEpoch()
{
     time_t today, todayEpoch;
     time(&today);
     struct tm *tm_today = localtime(&today);
     todayEpoch = mktime(tm_today);

     return todayEpoch;
}

int createFileOfSizeAndMTime(char *filename, int size, int daysOldInInSecs)
{
     char ch = '\n';
     
     FILE *fdest = fopen(filename, "w");
     if (fdest == NULL) return -1;

     fseek(fdest, size - 1, SEEK_CUR);
     fwrite(&ch, sizeof(char), 1, fdest);
     fclose(fdest);

     // set its mtime:
     struct stat st;
     struct utimbuf new_times;

     int todayEpoch = nowInEpoch();
     int daysOldInEpoch = todayEpoch - daysOldInInSecs;

     if(stat(filename, &st) != 0)
     {
          fprintf(stderr, "failed to open directory \"%s\" (%d: %s)\n",
                filename, errno, strerror(errno));
          return -1;
     }

     new_times.actime = st.st_atime; /* keep atime unchanged */
     new_times.modtime = daysOldInEpoch;   
     utime(filename, &new_times);

     return 0;

}

int createOneDirectory(char *dirPath)
{
     struct stat st = {0};

     if (stat(dirPath, &st) == -1) 
     {
         mkdir(dirPath, 0700);
     }
     return 0;
}


void createDirInDepth(char c, DirectoryFile_t list[], char str[], 
          int depth, char printFormat[], int *pi, int daysOldInSecs)
{    
     int j, i=*pi;     
     
     for(j=0; j<depth; j++)
     {
          printf("createDirInDepth: %s\n", list[i].dirName);                     // TREE LEVEL 1
   
          switch(strlen(printFormat))
          {
               case 7:
                    sprintf(str, printFormat, str, c, j);
                    break;
               case 9:
                    sprintf(str, printFormat, str, c, c, j);
                    break;
               case 11:
                    sprintf(str, printFormat, str, c, c, c, j);
                    break;
               default: 
                    printf("\n\tERROR!\n\n");
                    break;   
          }
          strcpy(list[i].dirName, str);
     
          createOneDirectory(list[i].dirName); 

          // Create files for each created directories
          if( randomFilesCreation(list[i].dirName, daysOldInSecs) ) continue;


     }
     *pi = ++i;
}

int randomNumberOfFiles()
{
     int max = MAX_NUMBER_OF_FILES_PER_DIRECTORY;
     int min = MIN_NUMBER_OF_FILES_PER_DIRECTORY;

     static int flag = 1;

     if (flag)
     {
          flag = 0;
          srand(time(NULL));
     }

     return rand() % (max - min +1) + min;
}

int randomFileSize()
{
     FILE_SIZES allSizes[] = { OneK, TenK, HundredK, OneMeg, FiveMegs, TenMegs };

     int max = 5;
     int min = 0;
  
     static int flag = 1;

     if (flag)
     {
          flag = 0;
          srand(time(NULL)); //seed);
     }

     int randomSize = rand() % ((max - min +1) + min);
     return allSizes[randomSize];
}


char * replaceSlashes(char *dirPath)
{
     char slash='/';
     char underscore='_';
     char *current_pos = strchr(dirPath, slash);
     while (current_pos) 
     {
          *current_pos = underscore;
          current_pos = strchr(current_pos,slash);
     }
     return dirPath;
}

int saveEntryToFile(filename, fSize, daysOldInSecs)
{
     return 0;
}

int randomFilesCreation(char *dirName, int daysOldInSecs)
{
          int nb, fSize;
          char filename[PATH_MAX];

          char *dirNameDup = strdup(dirName);
          char *strippedDir = replaceSlashes(dirNameDup);
          
          for(nb=0; nb<randomNumberOfFiles(); nb++)
          {
               fSize = randomFileSize();
               
               char *extension = nb % 3? TXT_EXTENSION:FOO_EXTENSION;
               sprintf(filename, "%s/%s_%d.%s", dirName, strippedDir, nb, extension);
               printf("filename: %s - fSize: %d - daysOld (sec.): %d\n", filename, fSize, daysOldInSecs);
               
               if( createFileOfSizeAndMTime(filename, fSize, daysOldInSecs) == -1) continue;

               // no need:
               //saveEntryToFile(filename, fSize, daysOldInSecs);
          }
          return 0;
}


DirectoryFile_t *mkTreeAndLeaves(char *path, int depth, 
     int breadth, DirectoryFile_t listOfDirectories[], int *pCounter, DAYS_OLD_IN_SEC daysOldInSecs)
{

     printf("\n\tmkTreeAndLeaves(path=%s, depth=%d, breadth=%d, daysOld (sec.)=%d)\n\n", 
          path, depth, breadth, daysOldInSecs );
     int i=0;
     
     char str[PATH_MAX]="";
     char str1[PATH_MAX]="";

     char c;
     char tlmSlash[PATH_MAX];
     strcpy(tlmSlash, path);
     strcat(tlmSlash, "/");

     for (c = 'A'; c <= 'Z'; ++c)
     {
          // TREE LEVEL 0 (relative root)
          strcpy(listOfDirectories[i].dirName, tlmSlash);   //        tlm/      
          strncat(listOfDirectories[i].dirName, &c, 1);     //        tlm/A
          createOneDirectory(listOfDirectories[i].dirName);    
          
          if( randomFilesCreation(listOfDirectories[i].dirName, daysOldInSecs) ) continue;

          
          // TREE LEVEL 1: A0, B0, etc.
          char printFormat[]="%s/%c%d";
          strcpy(str1, listOfDirectories[i].dirName);
          strcpy(str, str1);
          createDirInDepth(c, listOfDirectories, str, depth, printFormat, &i, daysOldInSecs);
 
          // TREE LEVEL 2: AA0, BB0, etc.
          strcpy(printFormat, "%s/%c%c%d");
          strcpy(str, str1);
          createDirInDepth(c, listOfDirectories, str, depth, printFormat, &i, daysOldInSecs);

          // TREE LEVEL 3: AAA0, BBB0, etc.
          strcpy(printFormat, "%s/%c%c%c%d");
          strcpy(str, str1);
          createDirInDepth(c, listOfDirectories, str, depth, printFormat, &i, daysOldInSecs);
     }
     *pCounter = i;

     return listOfDirectories;

}

int listAllDirectories(DirectoryFile_t list[], int counter)
{
     int i;
     for (i=0; i<counter; i++)
     {
          printf("\tDirectory: %s\n", list[i].dirName);
     }

     return 0;
}


int validateIndex(int ndx)
{
     return ndx <= DAYS_OLD_ARRAY_LENGTH && ndx >= 0;
}

char * parseAndGetDaysOldIndex(int argc, char ** argv, int *pDaysOldIndex, int *pDisplayIt)
{
     int opt;
     int optionsCounter = 1;
     
     while (( opt = getopt(argc, argv, OPTSTR)) != -1) 
     {
          switch (opt) {
               case 'i': optionsCounter++; 
                         *pDaysOldIndex = atoi(argv[optind]);
                         break;
               case 's': *pDisplayIt = 1; optionsCounter++; 
                         break;
          
               default:  break;
        }
     }
     optionsCounter++;

     if( argc - optionsCounter < 1)  usage();

     char *rootDir  = argv[optionsCounter];
     //printf("rootDir: %s - DaysOldIndex: %d - display: %d - optionsCounter: %d - optind: %d argv[%d]: %s\n", 
     //rootDir, *pDaysOldIndex, *pDisplayIt, optionsCounter, optind, optionsCounter, argv[optionsCounter]);
     if( !validateIndex( *pDaysOldIndex )  ) 
     {
          printf(" Incorrect dasyOld index. Should be between 0 and 5. Bailing out...\n");
          exit(EXIT_FAILURE);
     }

     return rootDir;
}


int main(int argc, char *argv[])
{
     int daysOldIndex=0, displayIt=0;
     char * rootDir = parseAndGetDaysOldIndex(argc, argv, &daysOldIndex, &displayIt);

     int directoriesCounter=0;
     DirectoryFile_t listOfDirectories[NUMBER_OF_DIRS];

     DAYS_OLD_IN_SEC daysOldPick[] = { OneDay, TwoDays, ThreeDays, FourDays, FiveDays };
     DAYS_OLD_IN_SEC daysOldInSecs = daysOldPick[daysOldIndex];

     mkTreeAndLeaves(rootDir, DEPTH, BREADTH, listOfDirectories, &directoriesCounter, daysOldInSecs);
     
     if( displayIt ) listAllDirectories(listOfDirectories, directoriesCounter);

     return 0;
}
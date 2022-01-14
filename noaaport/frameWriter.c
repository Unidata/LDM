#include "config.h"

#include "globals.h"
#include "noaaportFrame.h"
#include "frameWriter.h"

#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <log.h>

//  ========================================================================
static char	namedPipeFullName[PATH_MAX] = "";   //
static int 	fd;
//  ========================================================================

// mkfifo noaaportIngesterPipe is performed in the Python script
// Opening this pipe is performed once in this module
static int
fw_openPipe()
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int status = 0;

    log_add("Opening NOAAport pipeline...\n");
	log_flush_info();

    // Open pipe for write only
    if( (fd = open(namedPipeFullName, O_CREAT | O_WRONLY, mode ) )  == -1)
    {
        log_add("Cannot open named pipe! (%s)\n", namedPipeFullName);
        log_flush_warning();
        status = 1;
    }
    return status;
}

void
fw_start(const char * namedPipe)
{
	// named pipe full name: if NULL take the default
	if( namedPipe )
		strncpy(namedPipeFullName, namedPipe, strlen(namedPipe));

	fw_openPipe();	// review : atomic  O_CREAT
}

// Not used as we don't know when to close it...
static void
fw_closePipe()
{
    log_add("Closing NOAAport pipeline...\n");
	log_flush_info();
    close(fd);
}

// Send this frame to the noaaportIngester on its standard output through a pipe
//  pre-condition:  runMutex is     unLOCKED
//  pre-condition:  aFrameMutex is  LOCKED
//  post-condition: runMutex is     unLOCKED
//  post-condition:  aFrameMutex is  unLOCKED
int
fw_writeFrame(const Frame_t* aFrame)
{
    int             status      = 0;
    char            frameElements[PATH_MAX];

	//openNoaaportNamedPipe();	// review : atomic  O_CREAT

    // for printout purposes only:
    // <-- from here
    SeqNum_t  seqNum = aFrame->seqNum;
    RunNum_t  runNum = aFrame->runNum;
    sprintf(frameElements, "SeqNum: %lu - runNum: %lu \n", seqNum, runNum);
    log_info("   => Frame Out: %s", frameElements);
    // to here -->

    const unsigned char * sbnFrame    = aFrame->data;
    //int resp = write(fd, sbnFrame, strlen(sbnFrame) + 1); 		// <--- uncomment to start sending real SBN data
    int resp = write(fd, frameElements, strlen(frameElements) + 1); // <--- comment out to start sending real SBN data
    if( resp < 0 )
    {
        log_add("Write frame to pipe failure. (%s)\n", strerror(resp) );
        log_flush_warning();
        status = -1;
    }

	//closeNoaaportNamedPipe();
    return status;
}

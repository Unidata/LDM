#include "config.h"

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
#include "globals.h"

#include "hashTableImpl.h"
#include "frameWriter.h"

#ifndef PATH_MAX
#define PATH_MAX	120
#endif
//  ========================================================================
static char	namedPipeFullName[PATH_MAX] = "";   //
static int 	fd;
static int 	fw_openPipe(void);
//  ========================================================================


// aFrameWriterConfig's memory allocation is made in this function but freed in another
// function: "writer thread?", executed by a thread.

FrameWriterConf_t* fw_setConfig(int frameSize, const char* namedPipe)
{
	FrameWriterConf_t* aFrameWriterConfig = (FrameWriterConf_t*) malloc(sizeof(FrameWriterConf_t) );
	strncpy(aFrameWriterConfig->namedPipe, namedPipe, strlen(namedPipe));
	aFrameWriterConfig->frameSize = frameSize;

	return aFrameWriterConfig;
}

void
fw_start(FrameWriterConf_t* aFrameWriterConfig)
{

	// named pipe full name: if NULL take the default
	if( aFrameWriterConfig->namedPipe )
		strncpy(namedPipeFullName, aFrameWriterConfig->namedPipe, strlen(aFrameWriterConfig->namedPipe));

	fw_openPipe();	// review : atomic  O_CREAT

	free(aFrameWriterConfig);
}

static void
fw_closePipe()
{
    log_add("Closing NOAAport pipeline...\n");
	log_flush_info();
    close(fd);
}


void
fw_tearDown()
{
	fw_closePipe();
}



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

// Send this frame to the noaaportIngester on its standard output through a pipe
//  pre-condition:  runMutex is     unLOCKED
//  pre-condition:  aFrameMutex is  LOCKED

//  post-condition: runMutex is     unLOCKED
//  post-condition:  aFrameMutex is  unLOCKED
int
fw_writeFrame(Frame_t aFrame)
{
    int             status      = 0;
    char            frameElements[PATH_MAX];

	//openNoaaportNamedPipe();	// review : atomic  O_CREAT

    // for printout purposes
    uint32_t  seqNum = aFrame.seqNum;
    uint16_t  runNum = aFrame.runNum;
    sprintf(frameElements, "SeqNum: %lu - runNum: %lu \n", seqNum, runNum);
    log_add("   => Frame Out: %s\n\n", frameElements);
    log_flush_info();
    // We could call extractSeqNumRunCheckSum() to get seqNum, etc.

    const unsigned char * sbnFrame    = aFrame.data;
    //int resp = write(fd, sbnFrame, strlen(sbnFrame) + 1); // <----------- uncomment to start sending real SBN data
    int resp = write(fd, frameElements, strlen(frameElements) + 1); // <---   comment out to start sending real SBN data
    if( resp < 0 )
    {
        log_add("Write frame to pipe failure. (%s)\n", strerror(resp) );
        log_flush_warning();
        status = -1;
    }

	//closeNoaaportNamedPipe();
    return status;
}

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

#include "hashTableImpl.h"
#include "frameWriter.h"
//  ========================================================================
static char	namedPipeFullName[PATH_MAX] = NOAAPORT_NAMEDPIPE;   //
static int 	fd;
static int 	fw_openPipe(void);
//  ========================================================================


// aFrameWriterConfig's memory allocation is made in this function but freed in another
// function: "writer thread?", executed by a thread.

FrameWriterConf_t* fw_setConfig(int frameSize, char* namedPipe)
{
	FrameWriterConf_t* aFrameWriterConfig = (FrameWriterConf_t*) malloc(sizeof(FrameWriterConf_t) );
	strncpy(aFrameWriterConfig->namedPipe, namedPipe, strlen(namedPipe));
	aFrameWriterConfig->frameSize = frameSize;

	return aFrameWriterConfig;
}

void
fw_init(FrameWriterConf_t* aFrameWriterConfig)
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
    printf("Closing NOAAport pipeline...\n");
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

    printf("Opening NOAAport pipeline...\n");

    // Open pipe for write only
    if( (fd = open(namedPipeFullName, O_CREAT | O_WRONLY, mode ) )  == -1)
    {
        printf("Cannot open named pipe! (%s)\n", namedPipeFullName);
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
    printf("   => Frame Out: %s\n\n", frameElements);
    // We could call extractSeqNumRunCheckSum() to get seqNum, etc.

    const unsigned char * sbnFrame    = aFrame.data;
    //int resp = write(fd, sbnFrame, strlen(sbnFrame) + 1); // <----------- uncomment to start sending real SBN data
    int resp = write(fd, frameElements, strlen(frameElements) + 1); // <---   comment out to start sending real SBN data
    if( resp < 0 )
    {
        printf("Write frame to pipe failure. (%s)\n", strerror(resp) );
        status = -1;
    }

	//closeNoaaportNamedPipe();
    return status;
}

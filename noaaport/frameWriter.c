#include "config.h"

#include "frameWriter.h"

#include <stdio.h>
#include <unistd.h>
#include <log.h>
#include <errno.h>

extern bool testMode;

// Write this frame to standard output.

// Pipe it to noaaportIngester.
// $ blender <> <> <> | noaaportIngester

int
fw_writeFrame(const Frame_t* aFrame)
{
    int  status      = 0;
    char frameElements[PATH_MAX];
    ssize_t ret;
    // for printout purposes only:
    if(testMode)
    {
    	// Only send (seq, run#) pairs with option -s on the command line
		SeqNum_t  seqNum = aFrame->seqNum;
		RunNum_t  runNum = aFrame->runNum;
		sprintf(frameElements, "SeqNum: %lu - runNum: %lu \n", seqNum, runNum);
		// log_info("   => Frame Out: %s", frameElements);

		ret =  write(STDOUT_FILENO, frameElements, strlen(frameElements));
    }
    else
    {
    	ret =  write(STDOUT_FILENO, aFrame->data, aFrame->nbytes);	// <--- start sending real SBN data
    }

    if( ret <= 0 )
    {
        log_info("Write frame data to standard output failure. (%s)\n", strerror(errno) );
        log_add("Write frame data to standard output failure. (%s)\n", strerror(errno) );
        log_flush_warning();
        status = -1;
    }

    return status;
}

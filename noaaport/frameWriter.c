#include "config.h"

#include "frameWriter.h"

#include <stdio.h>
#include <unistd.h>
#include <log.h>
#include <errno.h>

// Write this frame to standard output.

// Pipe it to noaaportIngester.
// $ blender <> <> <> | noaaportIngester

int
fw_writeFrame(const Frame_t* aFrame)
{
    int  status      = 0;
    char frameElements[PATH_MAX];

    // for printout purposes only:
    // <---------------------------------------- from here --------------------------------------
    SeqNum_t  seqNum = aFrame->seqNum;
    RunNum_t  runNum = aFrame->runNum;
    sprintf(frameElements, "SeqNum: %lu - runNum: %lu \n", seqNum, runNum);
    log_info("   => Frame Out: %s", frameElements);
    // ------------------------------------ to here -------------------------------------------->

    const unsigned char * sbnFrame    = aFrame->data;
    // ssize_t ret =  write(STDOUT_FILENO, sbnFrame, strlen(sbnFrame));	// <--- uncomment to start sending real SBN data
    // instead of this:
    ssize_t ret =  write(STDOUT_FILENO, frameElements, strlen(frameElements));

    if( ret <= 0 )
    {
        log_info("Write frame data to standard output failure. (%s)\n", strerror(errno) );
        log_add("Write frame data to standard output failure. (%s)\n", strerror(errno) );
        log_flush_warning();
        status = -1;
    }

    return status;
}

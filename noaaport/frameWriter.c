#include "config.h"

#include "frameWriter.h"

#include <stdio.h>
#include <unistd.h>
#include <log.h>
#include <errno.h>
#include <inttypes.h>

// Write this frame to standard output.

// Pipe it to noaaportIngester.
// $ blender <> <> <> | noaaportIngester

int
fw_writeFrame(const Frame_t* aFrame)
{
    int status = 0;

	log_debug("SeqNum: %" PRI_SEQ_NUM " - RunNum: %" PRI_RUN_NUM,
	        aFrame->seqNum, aFrame->runNum);

   	// FOR INTERACTIVE TESTING, BE SURE TO REDIRECT stdout TO "/dev/null"
   	ssize_t ret =  write(STDOUT_FILENO, aFrame->data, aFrame->nbytes);
    if( ret <= 0 )
    {
        log_add("Write frame data to standard output failure. (%s)",
                strerror(errno) );
        status = -1;
    }

    return status;
}

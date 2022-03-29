#include "config.h"

#include "frameWriter.h"

#include <stdio.h>
#include <unistd.h>
#include <log.h>
#include <errno.h>
#include <inttypes.h>

/**
 * Function to write the SBN frame data to standard output.
 *
 * @param[in]  aFrame			An SBN frame
 *
 * @retval     0  				Success
 * @retval    <0       		  	Error
 */

int
fw_writeFrame(const Frame_t* aFrame)
{
    int status = 0;

	log_debug("ProdSeqNum: %u - DataBlkNum: %u", aFrame->prodSeqNum, aFrame->dataBlockNum);

   	// FOR INTERACTIVE TESTING, BE SURE TO REDIRECT stdout TO "/dev/null"
   	ssize_t ret = write(STDOUT_FILENO, aFrame->data, aFrame->nbytes);
    if( ret <= 0 )
    {
        log_add("Write frame data to standard output failure. (%s)",
                strerror(errno) );
        status = -1;
    }

    return status;
}

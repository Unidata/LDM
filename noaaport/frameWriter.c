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
 * @retval    -1       		  	I/O failure. `log_add()` called.
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
        log_add_syserr("Couldn't write frame data to standard output");
        status = -1;
    }

    return status;
}

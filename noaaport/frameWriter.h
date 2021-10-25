/*
 * frameWriter.h
 *
 *  Created on: Aug 27, 2021
 *      Author: Mustapha Iles
 */

#ifndef FRAMEWRITER_H_
#define FRAMEWRITER_H_

#include <limits.h>

#define NOAAPORT_NAMEDPIPE          "/tmp/noaaportIngesterPipe"

typedef struct frameWriter {
    int 		frameSize;
    char		namedPipe[PATH_MAX];
} FrameWriterConf_t;


#endif /* FRAMEWRITER_H_ */

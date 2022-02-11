/*
 * frameReader.h
 *
 *  Created on: Aug 27, 2021
 *      Author: Mustapha Iles
 */

#ifndef FRAMEREADER_H_
#define FRAMEREADER_H_

#define FIN 				 0
#define MAX_SERVERS			20				// hosts to connect to
#define INVALID_CHECKSUM	-2				// Frame checksum is invalid
#define SOCKET_READ_ERROR	-1

int reader_start( char* const*, int);

#endif /* FRAMEREADER_H_ */

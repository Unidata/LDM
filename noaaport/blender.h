/*
 * blender.h
 *
 *  Created on: Aug 24, 2021
 *      Author: Mustapha Iles
 */

#ifndef BLENDER_DOT_H
#define BLENDER_DOT_H

#define PORT                        9127
#define MIN_SOCK_TIMEOUT_MICROSEC   9000

#define MAX_HOSTS					20
#define	HOST_DESIGNATION			0
#define	PORT_DESIGNATION			1

static bool 	validateHostsInput(char * const *, int);
static int		isHostValid(  char  *, bool* );

#endif

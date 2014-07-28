/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: up7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the upstream LDM-7.
 */

#include "config.h"

#include "ldm.h"
#include "up7.h"
#include "log.h"
#include "rpcutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <rpc.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

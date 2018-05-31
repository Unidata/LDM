/**
 * This file tests printing thread identifiers.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: threadId_test.c
 *  Created on: May 24, 2018
 *      Author: Steven R. Emmerson
 */
#include <pthread.h>
#include <stdio.h>

static void*
printThread(void* arg)
{
    printf("Created thread=%lu\n", (unsigned long)pthread_self());
    return NULL;
}

int
main()
{
    /*
     * Example result:
     *
     *     self=140015246853952
     *     Created thread=140015238584064
     *
     * Conclusion: `pthread_t` is a friggin pointer!
     */
    printf("self=%lu\n", (unsigned long)pthread_self());

    pthread_t thread;
    (void)pthread_create(&thread, NULL, printThread, NULL);
    (void)pthread_join(thread, NULL);
}

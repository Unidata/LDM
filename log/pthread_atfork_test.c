/*
 * pthread_atfork_test.c
 *
 *  Created on: Dec 3, 2019
 *      Author: steve
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

pthread_mutex_t lock1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock2 = PTHREAD_MUTEX_INITIALIZER;

void
prepare(void)
{
    printf("preparing locks...\n");
    fflush(stdout);
    int status = pthread_mutex_lock(&lock1);
    if (status) {
    	fprintf(stderr, "Couldn't lock lock1: %s\n", strerror(status));
    	fflush(stderr);
    }
    status = pthread_mutex_lock(&lock2);
    if (status) {
    	fprintf(stderr, "Couldn't lock lock2: %s\n", strerror(status));
    	fflush(stderr);
    }
}

void
parent(void)
{
    printf("parent unlocking locks...\n");
    int status = pthread_mutex_unlock(&lock1);
    if (status)
    	fprintf(stderr, "Parent couldn't unlock lock1: %s\n", strerror(status));
    status = pthread_mutex_unlock(&lock2);
    if (status)
    	fprintf(stderr, "Parent couldn't unlock lock2: %s\n", strerror(status));
}

void
child(void)
{
    printf("child unlocking locks...\n");
    int status = pthread_mutex_unlock(&lock1);
    if (status)
    	fprintf(stderr, "Child couldn't unlock lock1: %s\n", strerror(status));
    status = pthread_mutex_unlock(&lock2);
    if (status)
    	fprintf(stderr, "Child couldn't unlock lock2: %s\n", strerror(status));
}

void *
thr_fn(void *arg)
{
    printf("thread started...\n");
    pause();
    return(0);
}

int
main(void)
{
    int status = pthread_atfork(prepare, parent, child);
    if (status) {
        fprintf(stderr, "can't install fork handlers: %s\n", strerror(status));
    }
    else {
		pthread_t tid;
		status = pthread_create(&tid, NULL, thr_fn, 0);
		if (status) {
			fprintf(stderr, "can't create thread: %s\n", strerror(status));
		}
		else {
			sleep(1);

			printf("parent about to fork...\n");
			fflush(stdout);

			pid_t pid = fork();
			if (pid < 0) {
				status = errno;
				fprintf(stderr, "fork failed: %s\n", strerror(status));
			}
			else if (pid == 0) { /* child */
				printf("child returned from fork\n");
				fflush(stdout);
				status = 0;
			}
			else {      /* parent */
				printf("parent returned from fork\n");
				fflush(stdout);
				status = 0;
			}
		}
    }

    exit(status);
}

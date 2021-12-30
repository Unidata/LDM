/*
 * NewBlender.h
 *  *
 *   *  Created on: Dec 29, 2021
 *    *      Author: Steven R. Emmerson
 *     */

#ifndef NOAAPORT_NEWBLENDER_H_
#define NOAAPORT_NEWBLENDER_H_

#include <stddef.h>
#include <stdint.h>

typedef int32_t RunNum;
typedef int64_t SeqNum;

typedef struct {
    RunNum runNum;
    SeqNum seqNum;
} RunSeqNum;

int runSeqNumComp(const void* node1, const void* node2);

typedef struct {
    bool   occupied;
    RunNum runNum;
    SeqNum seqNum;
    char*  data;
    size_t nbytes;
} Frame;

#define MAX_FRAMES 100

typedef struct {
    RunSeqNum head;
    RunSeqNum tail;
    Frame     frames[MAX_FRAMES];
} CircBuf;

// Root of the binary search tree
// void* root = NULL;
//
// // Element within the binary search tree
// typedef struct {
//     RunSeqNum runSeqNum;
//         int       index;
//         } Node;
//
//         #endif /* NOAAPORT_NEWBLENDER_H_ */
//

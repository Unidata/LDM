/* DO NOT EDIT THIS FILE. It was created by extractDecls */
/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */

#ifndef READER_H
#define READER_H

typedef struct reader     Reader;

/**
 * Returns a new reader. The client should call `readerFree()` when the reader
 * is no longer needed.
 *
 * This function is thread-safe.
 *
 * @param[in]  fifo     Pointer to FIFO that will read input
 * @param[in]  maxSize  Maximum amount to read in a single call in bytes.
 * @retval     NULL     Failure. `log_add()` called.
 * @return              Reader
 */
Reader*
readerNew(
    Fifo* const         fifo,
    const size_t        maxSize);

/**
 * Frees a reader. Closes the file descriptor given to `readerNew()`. Does not
 * free the FIFO given to `readerNew()`.
 *
 * @param[in] reader  Reader to be freed. May be NULL.
 */
void readerFree(
    Reader* const   reader);

/**
 * Executes a reader. Returns when end-of-input is encountered, the FIFO queue
 * is explicitly closed, or an error occurs. Logs a message on error. May be
 * called by `pthread_create()`.
 *
 * This function is thread-safe.
 *
 * @param[in]  arg   Pointer to reader.
 * @retval     &0    Success. End of input encountered.
 * @retval     &1    FIFO was closed.
 * @retval     &2    O/S failure. `log_flush()` called.
 */
void*
readerStart(
        void* const arg);

/**
 * Returns statistics since the last time this function was called or
 * `readerStart()` was called. This function is thread-safe.
 *
 * @param[in]  reader         The reader of input data.
 * @param[out] nbytes         Number of bytes read.
 * @param[out] fullFifoCount  Number of times the reader-thread had to wait on a
 *                            "full" FIFO.
 */
void readerGetStatistics(
    Reader* const           reader,
    unsigned long* const    nbytes,
    unsigned long* const    fullFifoCount);

#endif
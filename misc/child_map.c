/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
/* 
 * This module implements a map from process ID to command-line for a set of
 * child processes.
 *
 * This module is thread-compatible but not thread-safe.
 *
 * Unless otherwise noted, NULL arguments are not allowed.
 */
#include "config.h"

#include <search.h>
#include <stdlib.h>             /* malloc(), free() */
#include <string.h>             /* strcup() */
#include <sys/types.h>          /* pid_t */

#include "log.h"
#include "child_map.h"
#include "StringBuf.h"

typedef struct {
    char*       command;        /**< Command-line string of the child */
    pid_t       pid;            /**< Process ID of the child */
}       Entry;

struct child_map {
    void*       root;           /**< Root of the \c tsearch() map */
    StringBuf*  buf;            /**< Buffer for command-line building */
    unsigned    count;          /**< Number of entries in the map */
};


/**
 * Compares two child-map entries.
 *
 * @retval -1   The first argument is less than the second
 * @retval  0   The first argument equals the second
 * @retval  1   The first argument is greater than the second
 */
static int compare(
    const void* const   entry1,         /**< [in] Pointer to the first entry */
    const void* const   entry2)         /**< [in] Pointer to the second entry */
{
    const pid_t pid1 = ((const Entry*)entry1)->pid;
    const pid_t pid2 = ((const Entry*)entry2)->pid;

    return
        pid1 > pid2
            ? 1
            : pid1 == pid2
                ?  0
                : -1;
}


/**
 * Returns a new instance of a child-map.
 *
 * @retval NULL         Failure. \c log_start() called.
 * @retval !NULL        Pointer to a new instance.
 */
ChildMap* cm_new(void)
{
    ChildMap*   map = (ChildMap*)malloc(sizeof(ChildMap));

    if (NULL == map) {
        LOG_SERROR0("Couldn't allocate new child-map");
    }
    else {
        map->root = NULL;
        map->count = 0;
        map->buf = strBuf_new(132);

        if (NULL == map->buf) {
            LOG_SERROR0("Couldn't allocate command-line buffer");
            free(map);

            map = NULL;
        }
    }

    return map;
}


/**
 * Frees a child-map.
 */
void cm_free(
    ChildMap* const     map)            /**< [in] Pointer to the child-map or
                                         *   NULL */
{
    if (NULL != map) {
        while (0 < map->count--) {
            Entry* const    entry = *(Entry**)map->root;

            (void)tdelete(entry, &map->root, compare);
            free(entry->command);
            free(entry);
        }

        strBuf_free(map->buf);
        free(map);
    }
}


/*
 * Adds an entry to a child-map.
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int cm_add_string(
    ChildMap* const     map,            /**< [in/out] Pointer to the child-map
                                         */
    const pid_t         pid,            /**< [in] Process ID of the child.
                                         *   Must not already exist in map. */
    const char* const   command)        /**< [in] Command-line of the child.
                                         *   Defensively copied. */
{
    int    status;

    if (NULL == map) {
        LOG_START0("Null map argument");
        status = 1;
    }
    else if (NULL == command) {
        LOG_START0("Null command argument");
        status = 1;
    }
    else {
        status = cm_contains(map, pid);

        if (0 == status) {
            Entry* const    entry = (Entry*)malloc(sizeof(Entry));

            if (NULL == entry) {
                LOG_SERROR0("Couldn't allocate new entry");
                status = 2;
            }
            else {
                entry->command = strdup(command);

                if (NULL == entry->command) {
                    LOG_SERROR0("Couldn't duplicate command-line");
                    status = 2;
                }
                else {
                    entry->pid = pid;

                    if (NULL == tsearch(entry, &map->root, compare)) {
                        LOG_SERROR0("Couldn't add entry to map");
                        status = 2;
                    }
                    else {
                        map->count++;
                        status = 0;
                    }

                    if (0 != status)
                        free(entry->command);
                }                       /* "entry->command" allocated */

                if (0 != status)
                    free(entry);
            }                           /* "entry" allocated */
        }                               /* "pid" not in map */
    }                                   /* valid arguments */

    return status;
}


/*
 * Adds an entry to a child-map.
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int cm_add_argv(
    ChildMap* const map,    /**< [in/out] Pointer to the child-map */
    const pid_t     pid,    /**< [in] Process ID of the child.
                             *   Must not already exist in map. */
    char** const    argv)   /**< [in] Command-line of the child in
                             *   argument vector form. Last pointer
                             *   must be NULL. The strings are
                             *   defensively copied. */
{
    int                 status = 0;     /* success */

    if (NULL == map || NULL == argv) {
        status = 1;
    }
    else {
        int     i;

        (void)strBuf_clear(map->buf);

        for (i = 0; NULL != argv[i]; i++) {
            if (0 < i)
                (void)strBuf_appendString(map->buf, " ");
            if (0 != strBuf_appendString(map->buf, argv[i])) {
                LOG_SERROR1(
                        "Couldn't append to command-line buffer: \"%s\"",
                        argv[i]);
                status = 2;
                break;
            }
        }

        if (0 == status) {
            const char* command = strBuf_toString(map->buf);

            status = cm_add_string(map, pid, command);
        }
    }                                   /* argv != NULL */

    return status;
}


/**
 * Indicates if a child-map contains a particular entry.
 *
 * @retval 1    Yes, the child is in the child-map.
 * @retval 0    No, the child isn't in the child-map or \c map is \c NULL.
 */
int cm_contains(
    const ChildMap* const       map,    /**< [in] Pointer to the child-map */
    const pid_t                 pid)    /**< [in] Process ID of the child */
{
    int         contains;

    if (NULL == map || NULL == map->root) {
        contains = 0;
    }
    else {
        Entry   entry;

        entry.pid = pid;
        contains = tfind(&entry, &map->root, compare) != NULL;
    }

    return contains;
}


/**
 * Returns the command-line of an entry in a child-map.
 *
 * @retval NULL         \c map is \c NULL or the corresponding entry doesn't
 *                      exist
 * @retval !NULL        The command-line of the corresponding entry. Calling
 *                      \c cm_remove() on entry \c pid before dereferencing
 *                      this pointer results in undefined behavior.
 */
const char* cm_get_command(
    const ChildMap* const       map,    /**< [in] Pointer to the child-map */
    const pid_t                 pid)    /**< [in] Process ID of the child */
{
    const char* command;

    if (NULL == map) {
        command = NULL;
    }
    else {
        Entry               entry;
        const Entry* const* node;

        entry.pid = pid;
        node = tfind(&entry, &map->root, compare);

        command = (NULL == node)
            ? NULL
            : (*node)->command;
    }

    return command;
}


/**
 * Removes an entry from a child-map.
 *
 * @retval 0    Success. The entry was removed.
 * @retval 1    Usage error. \c log_start() called.
 * @retval 2    The entry wasn't in the map.
 */
int cm_remove(
    ChildMap* const     map,    /**< [in/out] Pointer to the child-map */
    const pid_t         pid)    /**< [in] Process ID of the entry to be removed
                                 */
{
    int         status;

    if (NULL == map) {
        LOG_START0("NULL map argument");
        status = 1;
    }
    else {
        if (NULL != map->root) {
            Entry               template;
            Entry* const*       node;

            template.pid = pid;
            node = tfind(&template, &map->root, compare);

            if (NULL == node) {
                status = 2;
            }
            else {
                Entry* const    entry = *node;

                (void)tdelete(&template, &map->root, compare);
                free(entry->command);
                free(entry);

                map->count--;
                status = 0;
            }
        }
    }

    return status;
}


/**
 * Returns the number of entries in a child-map.
 *
 * @return The number of entries in the child-map.
 */
unsigned cm_count(
    const ChildMap* const       map)    /**< [in] Pointer to the child-map or
                                         *   NULL, in which case \c 0 is 
                                         *   returned. */
{
    return NULL == map ? 0 : map->count;
}

#include "config.h"

#include <errno.h>
#include <search.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

#include "gb2def.h"
#include "ctbg2rdvar.h"
#include "proto_gemlib.h"

/**
 * A cache entry.
 */
typedef struct {
    char*    filename;         ///< filename of GRIB2 parameter table
    G2vars_t table;            ///< GRIB2 parameter table
    char     nameBuf[LLMXLN];  ///< buffer for filename
} Entry;

/**
 * The cache.
 */
static void* cache;

/**
 * Compares two cache entries.
 *
 * @param[in] e1  First cache entry.
 * @param[in] e2  Second cache entry.
 * @retval    <0  First entry is less than second.
 * @retval     0  Entries are equal.
 * @retval    >0  First entry is greater than second.
 */
static int
compare(
        const void* const e1,
        const void* const e2)
{
    return strcmp(((Entry*)e1)->filename, ((Entry*)e2)->filename);
}

/**
 * Initializes a cache entry.
 *
 * @param[in] entry     Cache entry to be initialized.
 * @param[in] filename  Filename associated with the entry.
 * @retval    0         Success. INFO-level message logged.
 * @retval    ENOMEM    Out-of-memory. `log_start()` called.
 * @retval    -31       Error reading file. `log_start()` called.
 */
static int
initEntry(
        Entry* const restrict entry,
        const char* const restrict filename)
{
    int status;

    strncpy(entry->nameBuf, filename, LLMXLN)[LLMXLN-1] = 0;
    entry->filename = entry->nameBuf;
    entry->table.nlines = 0;
    entry->table.info = 0;

    ctb_g2rdvar(entry->filename, &entry->table, &status);

    if (status) {
        LOG_START1("Couldn't read GRIB2 parameter file \"%s\"",
                entry->filename);
        status = -31;
    }
    else {
        uinfo("Read GRIB2 parameter file \"%s\"", entry->filename);

        void* node = tsearch(entry, &cache, compare);

        if (NULL == node) {
            LOG_SERROR0("Couldn't allocate new tsearch(3) node");
            status = ENOMEM;
        }
        else {
            status = 0;
        }
    }

    return status;
}

/**
 * Adds an entry to the cache.
 *
 * @param[in]  filename  Filename associated with the entry.
 * @param[out] entry     Returned entry.
 * @retval     0         Success. `*entry` is set.
 * @retval     ENOMEM    Out-of-memory. `log_start()` called.
 * @retval     -31       Error reading file. `log_start()` called.
 */
static int
addEntry(
        const char* const restrict filename,
        Entry** const restrict     entry)
{
    int    status;
    Entry* ent = malloc(sizeof(Entry));

    if (NULL == ent) {
        LOG_SERROR0("Couldn't allocate new cache entry");
        status = ENOMEM;
    }
    else {
        status = initEntry(ent, filename);

        if (status) {
            free(ent);
        }
        else {
            *entry = ent;
        }
    } // `ent` allocated

    return status;
}

/**
 * Finds a cache entry.
 *
 * @param[in]  filename  Filename associated with the entry.
 * @retval     NULL      Entry not found.
 * @return               Pointer to the associated entry.
 */
static inline Entry*
findEntry(
        const char* const restrict filename)
{
    Entry     entry;

    entry.filename = (char*)filename;  // `entry.filename` not modified => safe

    void* node = tfind(&entry, &cache, compare);

    return node ? *(Entry**)node : NULL;
}

/**
 * Returns the GRIB2 parameter table associated with a filename.
 *
 * @param[in]  filename  Filename associated with the entry.
 * @param[out] table     GRIB2 parameter table associated with the filename.
 * @retval     0         Success. `*table` is set.
 * @retval     ENOMEM    Out-of-memory. `log_start()` called.
 * @retval     -31       Couldn't read file. `log_start()` called.
 */
static int
getVarTable(
        const char* const restrict filename,
        G2vars_t** const restrict  table)
{
    int    status;
    Entry* entry = findEntry(filename);

    if (entry) {
        *table = &entry->table;
        status = 0;
    }
    else {
        status = addEntry(filename, &entry);

        if (0 == status)
            *table = &entry->table;
    }

    return status;
}

/**
 * Returns the GRIB2 parameter table corresponding to a given filename or a
 * filename constructed from subcomponents.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in]  vartbl     Filename of the desired GRIB2 Parameter table. If
 *                        NULL or the empty string, then the filename is
 *                        constructed using `cntr` and `ver`.
 * @param[in]  cntr       Abbreviation for the originating center. Ignored if
 *                        `vartbl && strlen(vartbl)`.
 * @param[in]  ver        Version number of the table. Ignored if
 *                        `vartbl && strlen(vartbl)`.
 * @param[out] g2vartbl   The returned table.
 * @param[out] filename   Filename associated with the returned table. May be
 *                        overwritten by the next invocation.
 * @param[out] iret       Return code:
 *                              0 = Success. `*g2vartbl` and `*filename` are
 *                                  set.
 *                            -31 = Error getting table. `log_log()` called.
 */
void
gb2_gtvartbl(
        char* const restrict        vartbl,
        char* const restrict        cntr,
        const int                   ver,
        G2vars_t** const restrict   g2vartbl,
        const char** const restrict filename,
        int* const restrict         iret)
{
    static char nameBuf[LLMXLN];

    /*
     * Check if user-supplied filename.  If not, then construct one.
     */
    if (vartbl && *vartbl) {
        strncpy(nameBuf, vartbl, LLMXLN)[LLMXLN-1] = 0;
    }
    else {
        (void)snprintf(nameBuf, LLMXLN, "g2vars%s%d.tbl", cntr, ver);
    }

    if (getVarTable(nameBuf, g2vartbl)) {
        log_log(LOG_ERR);
        *iret = 31;
    }
    else {
        *filename = nameBuf;
        *iret = 0;
    }

    return;
}

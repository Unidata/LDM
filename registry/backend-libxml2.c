/**
 * Copyright 2013 University Corporation for Atmospheric Research. All rights
 * reserved. See file COPYRIGHT in the top-level source-directory for copying
 * and redistribution conditions.
 * <p>
 * This module hides the decision on what database system to use.
 * <p>
 * This module implements the runtime database backend database API via the
 * "libxml2" library.
 */
#include <config.h>

#undef NDEBUG
#include <errno.h>
#include <libxml/parser.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "backend.h"
#include "ldmfork.h"
#include "log.h"
#include "registry.h"
#include "string_buf.h"

typedef struct {
    char*       path;           /* pathname of file */
    int         fd;             /* file-descriptor */
    int         exclusive;      /* exclusive (i.e., write) lock? */
    int         isLocked;       /* whether or not the file is locked */
} File;

/**
 * Returns a new file structure.
 *
 * Arguments
 *      path            Pointer to the pathname of the file. The user may free
 *                      upon return.
 *      exclusive       Exclusive (i.e., write) access?
 *      file            Pointer to a pointer to the file structure. Set upon
 *                      return.
 * Returns
 *      0               Success.
 *      EIO             I/O failure. "log_add()" called.
 *      ENOMEM          System failure. "log_add()" called.
 */
static RegStatus
fileNew(
    const char*         path,
    const int           exclusive,
    File** const        file)
{
    RegStatus   status;
    File*       f = (File*)malloc(sizeof(File));

    if (NULL == f) {
        log_add("Couldn't allocate %lu bytes", sizeof(File));
        status = ENOMEM;
    }
    else {
        f->isLocked = 0;
        f->exclusive = exclusive;
        f->path = strdup(path);

        if (NULL == f->path) {
            log_syserr("Couldn't duplicate string \"%s\"", path);
            status = ENOMEM;
            free(f);
        }
        else {
            *file = f;
            status = 0;                         /* success */
        }                                       /* "f->path" allocated */
    }                                           /* "f" allocated */

    return status;
}

/**
 * Returns the pathname of a file.
 *
 * Arguments
 *      file            Pointer to the file structure.
 * Returns The pathname of the file.
 */
static const char*
fileGetPath(
    const File* const   file)
{
    return file->path;
}

/**
 * Returns the size of a file.
 *
 * Arguments
 *      file            Pointer to the file structure.
 *      size            Pointer to the size of the file. Set upon return.
 * Returns
 *      0               Success. "*size" is set.
 *      EIO             I/O error. "log_add()" called.
 */
static RegStatus
fileGetSize(
    const File* const   file,
    off_t* const        size)
{
    struct stat     stat;

    if (fstat(file->fd, &stat) == -1) {
        log_syserr("Couldn't fstat(2) file \"%s\"", file->path);
        return EIO;
    }

    *size = stat.st_size;
    return 0;
}

/**
 * Locks a file. Idempotent.
 *
 * Arguments
 *      file            The file to be locked.
 * Returns
 *      0               Success
 *      EIO             I/O error. "log_add()" called. State of "file" is
 *                      unchanged.
 */
static RegStatus
fileLock(
    File* const file)
{
    RegStatus   status;
    
    if (file->isLocked) {
        status = 0;
    }
    else {
        int     fd;
        int     flags = 0;

#ifdef O_DSYNC
        flags |= O_DSYNC;
#endif
#ifdef O_RSYNC
        flags |= O_RSYNC;
#endif
        fd = file->exclusive
                ? open(file->path, O_RDWR | O_CREAT | flags, 0777)
                : open(file->path, O_RDONLY | flags);

        if (-1 == fd) {
            log_add_syserr("Couldn't open file \"%s\" for %s",
                file->path, file->exclusive ? "writing" : "reading");
            status = EIO;
        }
        else {
            if (ensure_close_on_exec(fd)) {
                log_add("Couldn't set file \"%s\" to close-on-exec", file->path);
                status = EIO;
            }
            else {
                struct flock        flock;

                flock.l_type = file->exclusive ? F_WRLCK : F_RDLCK;
                flock.l_whence = SEEK_SET;
                flock.l_start = 0;            /* from beginning */
                flock.l_len = 0;              /* to end */

                if (fcntl(fd, F_SETLKW, &flock) == -1) {
                    log_add_syserr("Couldn't lock file \"%s\"", file->path);
                    status = EIO;
                }
                else {
                    file->fd = fd;
                    file->isLocked = 1;
                    status = 0;
                }                               /* file locked */

                if (status)
                    close(fd);
            }
        }                                   /* "fd" open */
    } // File isn't locked => file isn't open

    return status;
}

/**
 * Unlocks a file.
 *
 * Arguments
 *      file            The file to be unlocked.
 * Returns
 *      0               Success
 *      EIO             I/O error. "log_add()" called. State of "file" is
 *                      unspecified.
 */
static RegStatus
fileUnlock(
    File* const file)
{
    RegStatus   status = 0;             /* success */

    if (file->isLocked) {
        struct flock        flock;

        flock.l_type = F_UNLCK;
        flock.l_whence = SEEK_SET;
        flock.l_start = 0;              /* from beginning */
        flock.l_len = 0;                /* to end */

        if (fcntl(file->fd, F_SETLK, &flock) == -1) {
            log_syserr("Couldn't unlock file \"%s\"", file->path);
            status = EIO;
        }
        else {
            file->isLocked = 0;
        }

        if (close(file->fd) == -1) {
            log_syserr("Couldn't close file \"%s\"", file->path);
            status = EIO;
        }
    }

    return status;
}

/**
 * Frees a file structure, releasing all resources.
 *
 * Arguments
 *      file            Pointer to the file.
 * Returns
 *      0               Success
 *      EIO             I/O failure. "log_add()" called. "file" is unchanged.
 */
static RegStatus
fileFree(
    File* const file)
{
    int         status = fileUnlock(file);      /* ensure unlocked */

    if (0 == status) {
        free(file->path);
        free(file);
    }

    return status;
}

/**
 * Deletes a file. The file must be exclusively locked.
 *
 * Arguments
 *      file            Pointer to the file.
 * Returns
 *      0               Success
 *      EACCES          The file isn't exclusively locked. "log_add()" called.
 *                      "file" is unchanged.
 *      EIO             I/O failure. "log_add()" called.  "file" is unchanged.
 */
static RegStatus
fileDelete(
    File* const file)
{
    int         status;

    if (!(file->isLocked && file->exclusive)) {
        log_add("File \"%s\" isn't exclusively locked. Can't delete.",
                file->path);
        status = EACCES;
    }
    else {
        if (unlink(file->path) == -1) {
            log_syserr("Couldn't unlink(2) file \"%s\"", file->path);
            status = EIO;
        }
        else {
            status = 0;
        }
    }

    return status;
}

typedef struct {
    StringBuf*  key;            /* name of node in registry key form */
    xmlNodePtr  xmlNode;        /* pointer to node in XML document */
} IndexElt;

struct backend {
    File*               file;           /* file structure */
    xmlDocPtr           doc;            /* XML document */
    xmlNodePtr          rootNode;       /* root node of XML document */
    IndexElt*           sortedIndex;    /* sorted XML node references */
    StringBuf*          content;        /* buffer for node content */
    int                 nodeCount;      /* number of XML nodes */
    int                 cursor;         /* index into "sortedIndex" */
    int                 forWriting;     /* XML file open for writing? */
    int                 modified;       /* XML document's been modified? */
    int                 isAcquired;     /* backend is acquired? */
};

static const char       DB_FILENAME[] = "registry.xml";
static const char       REGISTRY_ELTNAME[] = "registry";

/**
 * Returns the pathname of the XML file given a directory pathname.
 *
 * @param dir           Pathname of the parent directory.
 * @param path          Pathname of the XML file. Set upon return. User should
 *                      free when no longer needed.
 * @retval 0            Success. "*path" is set.
 * @retval ENOMEM       Out-of-memory. "log_add()" called.
 */
static RegStatus
getXmlFilePath(
    const char* const   dir,
    char** const        path)
{
    RegStatus           status;
    const size_t        nbytes = strlen(dir) + 1 + strlen(DB_FILENAME) + 1;
    char* const         p = (char*)malloc(nbytes);

    if (NULL == p) {
        log_syserr("Couldn't allocate %lu bytes for pathname", nbytes);
        status = ENOMEM;
    }
    else {
        (void)strcat(strcat(strcpy(p, dir), "/"), DB_FILENAME);
        *path = p;
        status = 0;
    }

    return status;
}

/*
 * Acts on a node in an XML tree.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key representing the desired
 *                      node.  Shall not be NULL.
 *      func            Pointer to the function to call on the found node.
 *      arg             Pointer to an argument to be passed to "func".
 * RETURNS:
 *      ENOENT          The given key doesn't match any node.
 *      else            The status returned by "func".
 */
static RegStatus
actOnNode(
    Backend*            backend,
    const char* const   key,
    int                 (*func)(xmlNodePtr, void*),
    void*               arg)
{
    RegStatus   status = 0;                     /* success */
    char* const dupKey = strdup(key);

    if (NULL == dupKey) {
        log_syserr("Couldn't duplicate string \"%s\"", key);
        status = ENOMEM;
    }
    else {
        char*           lasts;
        const char*     name;
        xmlNodePtr      parentNode = backend->rootNode;
        xmlNodePtr      childNode = NULL;

        for (name = strtok_r(dupKey, REG_SEP, &lasts); NULL != name;
                name = strtok_r(NULL, REG_SEP, &lasts)) {

            for (childNode = parentNode->children; NULL != childNode;
                    childNode = childNode->next) {
                if (xmlStrcmp((const xmlChar*)childNode->name,
                            (const xmlChar*)name) == 0)
                    break;
            }

            if (NULL == childNode) {
                break;
            }

            parentNode = childNode;
        }

        status = (NULL == childNode)
            ? ENOENT
            : func(childNode, arg);

        free(dupKey);
    }                                           /* "dupKey" allocated */

    return status;
}

/**
 * Returns the value of an XML node.
 *
 * Arguments
 *      node            Pointer to the XML node whose value is to be returned.
 *      value           Pointer to a pointer to the value of the XML node. Set
 *                      upon return. The use should call "free(*value)" when
 *                      the value is no longer needed.
 * Returns
 *      0               Success. "*value" is set.
 */
static RegStatus
getNodeContent(
    xmlNodePtr  node,
    void* const arg)
{
    *(char**)arg = (char*)xmlNodeGetContent(node);

    return 0;
}

/**
 * Deletes an XML node.
 *
 * Arguments
 *      node            Pointer to the XML node to be deleted.
 *      arg             An argument. Ignored.
 * Returns
 *      0               Success.
 */
static RegStatus
deleteNode(
    xmlNodePtr  node,
    void* const arg)
{
    xmlUnlinkNode(node);
    xmlFreeNode(node);

    return 0;
}

/*
 * Writes the in-memory XML document to its XML file if the XML file was opened
 * for writing and the in-memory XML document has been modified.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend structure. Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO             I/O error.  "log_add()" called.
 */
static RegStatus
writeXmlIfAppropriate(
    Backend* const      backend)
{
    RegStatus   status = 0;                     /* success */

    if (backend->forWriting && backend->modified) {
        int                 prevCompressMode = xmlGetCompressMode();
        int                 prevBlankMode = xmlKeepBlanksDefault(0);
        const char* const   path = fileGetPath(backend->file);

        if (xmlSaveFormatFile(path, backend->doc, 1) == -1) {
            log_add_syserr("Couldn't write XML file \"%s\"", path);
            log_flush_error();
            status = EIO;
        }

        xmlSetCompressMode(prevCompressMode);
        xmlKeepBlanksDefault(prevBlankMode);
    }                                           /* appropriate to write */

    return status;
}

/**
 * Returns the number of XML nodes that are descendants of an ancestor node.
 *
 * Arguments
 *      ancestor        Pointer to the XML ancestor node.
 * Returns
 *      The number of XML nodes that are descendants of the ancestor node.
 */
static size_t
getDescendantNodeCount(
    const xmlNodePtr    ancestor)
{
    size_t      count = 0;
    xmlNodePtr  childNode;

    for (childNode = ancestor->children; NULL != childNode;
            childNode = childNode->next) {
        if (XML_ELEMENT_NODE == childNode->type)
            count += 1 + getDescendantNodeCount(childNode);
    }

    return count;
}

/**
 * Adds XML nodes that are descendants of an ancestor node to an index.
 *
 * Arguments
 *      prefix          Pointer to the string prefix.
 *      nextElt         The next open element in the index.
 *      ancestor        Pointer to the ancestor node.
 *      eltCount        Pointer to the number of nodes added. Set only on
 *                      success.
 * Returns
 *      0               Success
 *      ENOMEM          Out-of-memory error. "log_add()" called.
 */
static RegStatus
recursiveAddDescendants(
    const char* const           prefix,
    IndexElt*                   nextElt,
    const xmlNodePtr            ancestor,
    int* const                  eltCount)
{
    RegStatus   status = 0;                     /* success */
    xmlNodePtr  childNode;
    IndexElt*   startElt = nextElt;

    for (childNode = ancestor->children; NULL != childNode;
            childNode = childNode->next) {
        if (XML_ELEMENT_NODE == childNode->type) {
            nextElt->xmlNode = childNode;

            if (0 == (status = sb_new(&nextElt->key, PATH_MAX)) &&
                0 == (status = sb_cat(nextElt->key, prefix, REG_SEP,
                        childNode->name, NULL))) {
                int     nelt;

                if (0 == (status =
                        recursiveAddDescendants(sb_string(nextElt->key),
                            nextElt+1, childNode, &nelt))) {
                    nextElt += 1 + nelt;
                }

                if (status)
                    sb_free(nextElt->key);
            }                                   /* "nextElt->key" allocated */
        }                                       /* child-node is an element */

        if (status)
            break;
    }

    /* Cleanup if necessary. */
    if (status) {
        /*
         * "nextElt" points to unset element. "childNode" points to node in
         * which error occured.
         */
        for (childNode = childNode->prev; NULL != childNode;
                childNode = childNode->prev) {
            --nextElt;
            sb_free(nextElt->key);
        }
    }
    else {
        *eltCount = nextElt - startElt;
    }

    return status;
}

/**
 * Adds all XML nodes to a sorted index.
 *
 * Arguments
 *      back            Pointer to the backend structure.
 * Returns
 *      0               Success
 *      ENOMEM          Out-of-memory error. "log_add()" called.
 */
static RegStatus
addDescendants(
    Backend* const      back)
{
    IndexElt*   nextElt = back->sortedIndex;
    int         eltCount;

    return recursiveAddDescendants("", nextElt, back->rootNode, &eltCount);
}

/**
 * Compares two sorted index elements.
 *
 * Arguments
 *      p1              Pointer to the first index element.
 *      p2              Pointer to the second index element.
 * Returns a value less than, equal to, or greater than zero as the first
 * element is considered less than, equal to, or greater than the second
 * element, respectively.
 */
static int
compareIndexElts(
    const void* const   p1,
    const void* const   p2)
{
    const IndexElt* const       e1 = (IndexElt*)p1;
    const IndexElt* const       e2 = (IndexElt*)p2;

    return strcmp(sb_string(e1->key), sb_string(e2->key));
}

/**
 * Builds an index of XML tree nodes in sorted order.
 *
 * Arguments
 *      back            Pointer to the backend structure.
 * Returns
 *      0               Success
 *      ENOMEM          Out-of-memory. "log_add()" called.
 */
static RegStatus
buildSortedIndex(
    Backend*    back)
{
    RegStatus   status = 0;                     /* success */

    int         nodeCount = getDescendantNodeCount(back->rootNode);

    if (0 < nodeCount) {
        IndexElt*   index = (IndexElt*)malloc(nodeCount * sizeof(IndexElt));

        if (NULL == index) {
            log_syserr("Couldn't allocate %d elements for sorted index",
                    nodeCount);
            status = ENOMEM;
        }
        else {
            back->sortedIndex = index;
            status = addDescendants(back);

            if (status) {
                free(index);
                back->sortedIndex = NULL;
            }
            else {
                qsort(index, nodeCount, sizeof(IndexElt), compareIndexElts);
                back->nodeCount = nodeCount;
            }
        }                                       /* "index" allocated */
    }                                           /* "nodeCount > 0" */

    return status;
}

/*
 * Frees the index of sorted XML nodes. Idempotent.
 *
 * Arguments
 *      back            Pointer to the backend structure.
 */
static void
freeSortedIndex(
    Backend* const      back)
{
    if (back->sortedIndex) {
        IndexElt*       elt;

        for (elt = back->sortedIndex; elt < back->sortedIndex + back->nodeCount;
                elt++) {
            sb_free(elt->key);
        }

        free(back->sortedIndex);
        back->sortedIndex = NULL;
        back->nodeCount = 0;
    }
}

/**
 * Creates a new document.
 *
 * Arguments
 *      back            Pointer to the backend structure.
 * Returns
 *      0               Success. Empty document is created.
 *      ENOMEM          Out-of-memory error. "log_add()" called.
 */
static RegStatus
createNewDocument(
    Backend* const      back)
{
    RegStatus   status = 0;                     /* success */
    xmlDocPtr   doc = xmlNewDoc((const xmlChar*)"1.0");

    if (NULL == doc) {
        log_add_syserr("Couldn't create new XML document");
        log_flush_error();
        status = ENOMEM;
    }
    else {
        xmlNodePtr      rootNode = xmlNewDocNode(doc, NULL, 
                (const xmlChar*)REGISTRY_ELTNAME, NULL);

        if (NULL == rootNode) {
            log_add_syserr("Couldn't create root-node of XML document");
            log_flush_error();
            status = ENOMEM;
        }
        else {
            (void)xmlDocSetRootElement(doc, rootNode);
        }

        if (status) {
            xmlFreeDoc(doc);
        }
        else {
            back->doc = doc;
            back->rootNode = rootNode;
            back->modified = 1;
        }
    }                                           /* "doc" allocated */

    return status;
}

/**
 * Parses an XML file into an internal document.
 *
 * Arguments
 *      back            Pointer to the backend structure.
 *      path            Pathname of the XML file.
 * Returns
 *      0               Success.
 *      EIO             I/O error. "log_add()" called.
 */
static RegStatus
parseXmlFile(
    Backend* const      back,
    const char* const   path)
{
    RegStatus   status;
    int         prevBlankMode = xmlKeepBlanksDefault(0);
    xmlDocPtr   doc = xmlParseFile(path);

    if (NULL == doc) {
        log_add("Couldn't parse XML file \"%s\"", path);
        status = EIO;
    }
    else {
        xmlNodePtr  rootNode = xmlDocGetRootElement(doc);

        if (NULL == rootNode) {
            log_add("XML file \"%s\" is empty", path);
            status = EIO;
        }
        else {
            back->doc = doc;
            back->rootNode = rootNode;
            back->modified = 0;
            status = 0;                         /* success */
        }
    }

    xmlKeepBlanksDefault(prevBlankMode);

    return status;
}

/*
 * Acquires the XML backend.
 *
 * ARGUMENTS:
 *      back            The backend structure.  Shall not be NULL.  The client
 *                      should call "releaseBackend(back)" to allow other
 *                      threads-of-control to access the database.
 *      dir             Pathname of the parent directory of the database.
 *                      Shall not be NULL.  The client may free it upon return.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called. "*back"
 *                      is unchanged.
 *      ENOMEM          System error.  "log_add()" called. "*back" is
 *                      unchanged.
 */
static RegStatus
acquireBackend(
    Backend* const      back)
{
    RegStatus   status = fileLock(back->file);

    log_assert(!back->isAcquired);

    if (0 == status) {
        off_t       size;

        if (0 == (status = fileGetSize(back->file, &size))) {
            status = (0 == size)
                ? createNewDocument(back)
                : parseXmlFile(back, fileGetPath(back->file));

            if (0 == status) {
                if (0 == (status = buildSortedIndex(back))) {
                    back->isAcquired = 1;
                }
            }
        }                           /* got size of XML file */

        if (status)
            fileUnlock(back->file);
    }                               /* file locked */

    return status;
}

/*
 * Releases the XML backend.
 *
 * ARGUMENTS:
 *      back            Pointer to the backend XML structure.  Shall have been
 *                      set by "acquireBackend()".
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
static RegStatus
releaseBackend(
    Backend* const      back)
{
    RegStatus   status = 0;             /* success */

    log_assert(back->isAcquired);

    if (0 == (status = writeXmlIfAppropriate(back))) {
        int     stat = fileUnlock(back->file);

        if (0 == stat) {
            back->isAcquired = 0;
        }

        if (0 == status) {
            status = stat;
        }
    }

    freeSortedIndex(back);
    xmlFreeDoc(back->doc);

    return status;
}

/*
 * Indicates if an XML node is a leaf-node. An XML node is a leaf-node if it
 * has no child-nodes of type ELEMENT.
 *
 * Arguments
 *      node            Pointer to the XML node.
 * Returns
 *      0               if and only if the node isn't a leaf-node.
 */
static int
isLeafNode(
    xmlNodePtr  node)
{
    xmlNodePtr  child;

    for (child = node->children; NULL != child; child = child->next) {
        if (XML_ELEMENT_NODE == child->type)
            return 0;
    }

    return 1;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/*
 * Opens the backend database.
 *
 * ARGUMENTS:
 *      backend         Pointer to pointer to backend structure.  Shall not be
 *                      NULL.  Upon successful return, "*backend" will be set.
 *                      The client should call "beClose(*backend)" when the
 *                      backend is no longer needed.
 *      dir             Pathname of the parent directory of the database.
 *                      Shall not be NULL.  The client may free it upon return.
 *      forWriting      Open the database for writing? 0 <=> no
 * RETURNS:
 *      0               Success.  "*backend" is set.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beOpen(
    Backend** const     backend,
    const char* const   dir,
    int                 forWriting)
{
    RegStatus   status;
    Backend*    back = (Backend*)malloc(sizeof(Backend));

    if (NULL == back) {
        log_syserr("Couldn't allocate %lu bytes", (long)sizeof(Backend));
        status = ENOMEM;
    }
    else {
        char*   path;

        if (0 == (status = getXmlFilePath(dir, &path))) {
            back->isAcquired = 0;
            back->doc = NULL;
            back->rootNode = NULL;
            back->forWriting = forWriting;
            back->cursor = -1;
            back->sortedIndex = NULL;
            back->nodeCount = 0;
            status = sb_new(&back->content, PATH_MAX);

            if (0 == status) {
                status = fileNew(path, forWriting, &back->file);
            }                                   /* "back->content" allocated */

            free(path);
        }                                       /* "path" allocated */

        if (status) {
            free(back);
        }
        else {
            *backend = back;
        }
    }                                           /* "back" allocated */

    return status;
}

/*
 * Closes the backend database.
 *
 * ARGUMENTS:
 *      back            Pointer to the database.  Shall have been set by
 *                      "beOpen()" or may be NULL.  Upon return, "backend"
 *                      shall not be used again.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beClose(
    Backend* const      back)
{
    RegStatus   status = 0;                     /* success */
    
    if (NULL != back) {
        if (back->isAcquired) {
            status = releaseBackend(back);
        }

        fileFree(back->file);
        sb_free(back->content);
        free(back);
    }

    return status;
}

/*
 * Resets the backend database.  This function shall be called only when
 * nothing holds the database open.
 *
 * ARGUMENTS:
 *      path            Pathname of the database directory.  Shall not be NULL.
 *                      The client can free it upon return.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beReset(
    const char* const   path)
{
    return 0;
}

/*
 * Removes the backend database.  This function shall be called only when
 * nothing holds the database open.
 *
 * ARGUMENTS:
 *      dir             Pathname of the parent directory of the database.
 *                      Shall not be NULL.  The client may free it upon return.
 * RETURNS:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beRemove(
    const char* const   dir)
{
    char*       path;
    RegStatus   status = getXmlFilePath(dir, &path);

    if (0 == status) {
        File*   file = NULL; // Assignment quiets scan-build(1)

        if (0 == (status = fileNew(path, 1, &file))) {
            if (0 == (status = fileLock(file)))
                status = fileDelete(file);

            fileFree(file);
        }                                       /* "file" allocated */

        free(path);
    }                                           /* "path" allocated */

    return status;
}

/*
 * Maps a key to a string.  Overwrites any pre-existing entry.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.  Shall not be NULL.
 *                      Shall not contain any spaces.
 *      value           Pointer to the string value.  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EINVAL          "key" contains a space.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error. "log_add()" called.
 */
RegStatus
bePut(
    Backend*            backend,
    const char* const   key,
    const char* const   value)
{
    RegStatus   status;

    if (strchr(key, ' ') != NULL) {
        log_add("Key \"%s\" has a space", key);
        status = EINVAL;
    }
    else {
        char* const dupKey = strdup(key);

        if (NULL == dupKey) {
            log_syserr("Couldn't duplicate string \"%s\"", key);
            status = ENOMEM;
        }
        else {
            status = acquireBackend(backend);

            if (0 == status) {
                char*           lasts;
                const char*     name;
                xmlNodePtr      parentNode = backend->rootNode;
                xmlNodePtr      childNode = NULL;

                for (name = strtok_r(dupKey, REG_SEP, &lasts); NULL != name;
                        name = strtok_r(NULL, REG_SEP, &lasts)) {

                    for (childNode = parentNode->children; NULL != childNode;
                            childNode = childNode->next) {
                        if (xmlStrcmp(childNode->name, (const xmlChar*)name)
                                == 0)
                            break;
                    }

                    if (NULL == childNode) {
                        childNode = xmlNewTextChild(parentNode, NULL, 
                                (const xmlChar*)name, NULL);
                        backend->modified = 1;
                    }

                    parentNode = childNode;
                }

                if (NULL != childNode) {
                    xmlNodeSetContent(childNode, (const xmlChar*)value);
                    backend->modified = 1;
                }

                status = releaseBackend(backend);
            }                           /* backend acquired */

            free(dupKey);
        }                               /* "dupKey" allocated */
    }                                   /* no space in "key" */

    return status;
}

/*
 * Returns the string to which a key maps.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.  Shall not be NULL.
 *      value           Pointer to a pointer to the string value.  Shall not be
 *                      NULL.  "*value" shall point to the 0-terminated string
 *                      value upon successful return.  The client should call
 *                      "free(*value)" when the value is no longer needed.
 * RETURNS:
 *      0               Success.  "*value" points to the string value.
 *      ENOENT          The given key doesn't match any entry.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beGet(
    Backend*            backend,
    const char* const   key,
    char** const        value)
{
    RegStatus   status = acquireBackend(backend);

    if (0 == status) {
        int     stat;

        status = actOnNode(backend, key, getNodeContent, value);
        stat = releaseBackend(backend);

        if (0 == status)
            status = stat;
    }                                           /* backend acquired */

    return status;
}

/*
 * Deletes an entry in the database.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 *      key             Pointer to the 0-terminated key.
 * RETURNS:
 *      0               Success.  The entry associated with the key was deleted.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beDelete(
    Backend* const      backend,
    const char* const   key)
{
    RegStatus   status = acquireBackend(backend);

    if (0 == status) {
        int     stat;

        status = actOnNode(backend, key, deleteNode, NULL);

        if (ENOENT == status) {
            status = 0;
        }
        else if (0 == status) {
            backend->modified = 1;
        }

        stat = releaseBackend(backend);

        if (0 == status)
            status = stat;
    }

    return status;
}

/*
 * Synchronizes the database (i.e., flushes any cached data to disk) if
 * appropriate.
 *
 * ARGUMENTS:
 *      backend         Pointer to the database.  Shall have been set by
 *                      "beOpen()".  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beSync(
    Backend* const      backend)
{
    /* The XML file is always synchronized. */
    return 0;
}

/*
 * Initializes the cursor.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS
 *      0               Success.
 *      EINVAL          The backend database already has an active cursor.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus
beInitCursor(
    Backend* const      backend)
{
    return acquireBackend(backend);             /* NB: file locked */
}

/*
 * Sets the cursor to reference the first entry in the backend
 * database whose key is greater than or equal to a given key.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 *      key             Pointer to the starting key.  Shall not be NULL.  The
 *                      empty string obtains the first entry in the database,
 *                      if it exists.
 * RETURNS
 *      0               Success.
 *      EINVAL          The cursor is not initialized.
 *      ENOENT          The database is empty.
 *      EIO             Backend database error.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus
beFirstEntry(
    Backend* const      backend,
    const char* const   key)
{
    int         i;

    for (i = 0; i < backend->nodeCount; i++) {
        const IndexElt* const   elt = backend->sortedIndex + i;

        if (strcmp(sb_string(elt->key), key) >= 0 && isLeafNode(elt->xmlNode)) {
            backend->cursor = i;
            return 0;
        }
    }

    return ENOENT;
}

/*
 * Advances the cursor to the next entry.
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS
 *      0               Success.
 *      ENOENT          No more entries.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beNextEntry(
    Backend* const      backend)
{
    int     i;

    for (i = backend->cursor + 1; i < backend->nodeCount; i++) {
        const IndexElt* const   elt = backend->sortedIndex + i;

        if (isLeafNode(elt->xmlNode)) {
            backend->cursor = i;
            return 0;
        }
    }

    return ENOENT;
}

/*
 * Frees the cursor.  Should be called after every successful call to
 * beInitCursor().
 *
 * ARGUMENTS:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * RETURNS:
 *      0               Success.
 *      EIO             Backend database error.  "log_add()" called.
 */
RegStatus
beFreeCursor(
    Backend* const      backend)
{
    return releaseBackend(backend);
}

/*
 * Returns the key of the cursor.
 *
 * Arguments:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * Returns:
 *      Pointer to the key.  Shall not be NULL if beFirstEntry() or 
 *      beNextEntry() was successful.
 */
const char*
beGetKey(
    const Backend* const        backend)
{
    return sb_string(backend->sortedIndex[backend->cursor].key);
}

/*
 * Returns the value of the cursor. If the last "beFirstEntry()" or
 * "beNextEntry()" was successful, then NULL shall not be returned unless an
 * error occurs.
 *
 * Arguments:
 *      backend         Pointer to the backend database.  Shall have been
 *                      set by beOpen().  Shall not be NULL.
 * Returns:
 *      NULL            Error occurred. "log_add()" called.
 *      else            Pointer to the value.
 */
const char*
beGetValue(
    const Backend* const        backend)
{
    const char* value = NULL;
    IndexElt*   elt = &backend->sortedIndex[backend->cursor];
    const char* key = sb_string(elt->key);
    char*       content = (char*)xmlNodeGetContent(elt->xmlNode);

    if (NULL == content) {
        log_add_syserr("Couldn't get value of key \"%s\"", key);
        log_flush_error();
    }
    else {
        if (sb_set(backend->content, content, NULL)) {
            log_add("Couldn't get value of key \"%s\"", key);
        }
        else {
            value = sb_string(backend->content);
        }

        xmlFree(content);
    }                                           /* "content" allocated */

    return value;
}

/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This file implements the API for registry nodes.
 *
 *   This module hides the decision on how to implement the node and value
 *   structures.
 *
 *   The functions in this file are thread-compatible but not thread-safe.
 */
#include <config.h>

#undef NDEBUG
#include "log.h"
#include "misc.h"
#include "node.h"
#include "registry.h"
#include "string_buf.h"

#include <errno.h>
#include <search.h>
#include <string.h>

struct regNode {
    char*       absPath;
    const char* name;
    void*       children;
    /* Invariant: values and deletedValues are disjoint sets */
    void*       values;
    void*       deletedValues;
    /* Invariant: the parent-node has this child-node */
    RegNode*    parent;
    int         modified;
    int         deleted;
};

struct valueThing {
    char*       name;
    char*       string;
    int         status;
};

static RegStatus        _status = 0;    /* visitation status */
static ValueFunc        _valueFunc;     /* value visitation function */
static NodeFunc         _nodeFunc;      /* node visitation function */

/******************************************************************************
 * Private Functions:
 ******************************************************************************/

/*
 * Allocates a new "ValueThing" instance.  The "name" and "string" fields will
 * be NULL and the "status" field will be 0.
 *
 * Arguments:
 *      valueThing      Pointer to a pointer to the new instance.  Shall not be
 *                      NULL.  Set upon successful return.
 * Returns:
 *      0               Success.  "*valueThing" is set.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus newValueThing(
    ValueThing** const  valueThing)
{
    RegStatus   status;
    ValueThing* vt = (ValueThing*)reg_malloc(sizeof(ValueThing), &status);

    if (0 != status) {
        log_add("Couldn't allocate a new ValueThing");
    }
    else {
        vt->name = NULL;
        vt->string = NULL;
        vt->status = 0;
        *valueThing = vt;
        status = 0;
    }

    return status;
}

/*
 * Frees a ValueThing.
 *
 * Arguments:
 *      valueThing      Pointer to the instance to be freed.  May be NULL.
 */
static void freeValueThing(
    ValueThing* const   valueThing)
{
    if (NULL != valueThing) {
        free(valueThing->name);
        free(valueThing->string);
        free(valueThing);
    }
}

/*
 * Compares two values.
 *
 * Arguments:
 *      value1          Pointer to the first ValueThing
 *      value2          Pointer to the second ValueThing
 * Returns:
 *      A value less than, equal to, or greater than zero as the first argument
 *      is considered less than, equal to, or greater than the second argument,
 *      respectively.
 */
static int compareValueThings(
    const void* const   value1,
    const void* const   value2)
{
    return strcmp(((const ValueThing*)value1)->name,
        ((const ValueThing*)value2)->name);
}

/*
 * Compares two nodes.
 *
 * Arguments:
 *      node1           Pointer to the first node
 *      node2           Pointer to the second node
 * Returns:
 *      A value less than, equal to, or greater than zero as the first node is
 *      considered less than, equal to, or greater than the second node,
 *      respectively.
 */
static int compareNodes(
    const void* const   node1,
    const void* const   node2)
{
    return strcmp(((const RegNode*)node1)->name,
        ((const RegNode*)node2)->name);
}

/*
 * Adds a child node to a parent node.
 *
 * Arguments:
 *      parent          Pointer to the parent node.  May be NULL.
 *      child           Pointer to the child node.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      EEXIST          A value with the same name exists in the parent node.
 *                      "log_add()" called.
 */
static RegStatus addChild(
    RegNode* const              parent,
    const RegNode* const        child)
{
    RegStatus   status;

    if (NULL == parent) {
        /* The child-node is the root node */
        status = 0;
    }
    else {
        ValueThing      vt;

        vt.name = (char*)child->name;   /* safe cast */

        if (NULL != tfind(&vt, &parent->values, compareValueThings)) {
            log_add("Node \"%s\" has a value named \"%s\"", parent->absPath,
                    vt.name);
            status = EEXIST;
        }
        else {
            if (NULL == tsearch(child, &parent->children, compareNodes)) {
                log_syserr("tsearch() failure");
                status = ENOMEM;
            }
            else {
                log_assert(parent == child->parent);
                status = 0;
            }
        }

        if (status)
            log_add("Couldn't add child-node \"%s\" to parent-node \"%s\"",
                    child->name, parent->absPath);
    }

    return status;
}

/*
 * Initializes the name field and absolute path name field of a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      parent          Pointer to the parent node.  Shall be NULL if and only
 *                      if "node" is a root-node.
 *      name            Pointer to the name of the node.  Shall not be NULL.
 * Returns:
 *      0               Success.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus initNameAndPath(
    RegNode* const              node,
    const RegNode* const        parent,
    const char* const           name)
{
    RegStatus   status;
    const char* prefix =
        (NULL == parent)
            ? ""
            : (0 == *parent->name
                ? ""
                : parent->absPath);
    size_t      prefixLen = strlen(prefix);
    size_t      nbytes = prefixLen + 1 + strlen(name) + 1;
    char*       buf = (char*)reg_malloc(nbytes, &status);

    if (0 == status) {
        (void)strcpy(strcpy(strcpy(buf, prefix) + prefixLen,
            REG_SEP) + 1, name);
        node->absPath = buf;
        node->name = buf + prefixLen + 1;
    }

    return status;
}

/*
 * Allocates a new, empty node.  The node is added to its parent.
 *
 * Arguments:
 *      parent          Pointer to the parent node.  Shall be NULL if and only
 *                      if the node will be a root-node.
 *      name            Pointer to the name for the node.  Shall not be NULL.
 *                      The client may free upon return.
 *      node            Pointer to a pointer to the newly-allocated node.  Set
 *                      upon successful return.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      EEXIST          A value with the same name exists in the parent node.
 *                      "log_add()" called.
 */
static RegStatus newNode(
    RegNode* const      parent,
    const char* const   name,
    RegNode** const     node)
{
    RegStatus   status;
    size_t      nbytes = sizeof(RegNode);
    RegNode*    nod = (RegNode*)reg_malloc(nbytes, &status);

    if (0 == status) {
        if (0 == (status = initNameAndPath(nod, parent, name))) {
            nod->parent = parent;
            nod->children = NULL;
            nod->values = NULL;
            nod->deletedValues = NULL;
            nod->modified = 0;
            nod->deleted = 0;

            if (0 == (status = addChild(parent, nod)))
                *node = nod;            /* success */

            if (status)
                free(nod->absPath);
        }                               /* "nod->absPath" allocated */

        if (status)
            free(nod);
    }                                   /* "nod" allocated */

    if (status) {
        if (NULL == parent)
            log_add("Couldn't create root-node");
        else
            log_add("Couldn't create child-node \"%s\" of parent-node \"%s\"",
                name, parent->absPath);
    }

    return status;
}

/*
 * Frees a tree of ValueThings.
 *
 * Arguments:
 *      root            Pointer to a pointer to the root-node of the tree.
 *                      Shall not be NULL.
 */
static void freeValues(
    void** const        root)
{
    while (NULL != *root) {
        ValueThing*     vt = *(ValueThing**)*root;

        (void)tdelete(vt, root, compareValueThings);
        freeValueThing(vt);
    }
}

/*
 * Visits the values of a value-tree in the natural order of their names.  Does
 * nothing if "_status" is non-zero.
 *
 * Preconditions:
 *      _valueFunc      Points to the function to be called for each value.
 *                      Shall not be NULL.
 * Arguments:
 *      node            Pointer to a pointer to the value to be visited.
 *      visit           Visitation-order identifier
 *      level           Level in the node-tree
 */
static void visitValue(
    const void* const   value,
    const VISIT         visit,
    const int           level)
{
    if (0 == _status) {
        /* "postorder" is used to ensure a sorted traversal */
        if (postorder == visit || leaf == visit)
            _status = _valueFunc(*(ValueThing* const*)value);
    }
}

static RegStatus visitNodes(
    RegNode* const      node,
    const NodeFunc      func);

/*
 * Visits the child-nodes of a node in the natural order of their path names.
 * Does nothing if "_status" is non-zero.
 *
 * Preconditions:
 *      _nodeFunc       Points to the function to be called for each child-node.
 *                      Shall not be NULL.
 * Arguments:
 *      node            Pointer to a pointer to the node to be visited.
 *      visit           Visitation-order identifier
 *      level           Level in the node-tree
 */
static void visitNode(
    const void* const   node,
    const VISIT         visit,
    const int           level)
{
    if (0 == _status) {
        /* "postorder" is used to ensure a sorted traversal */
        if (endorder == visit || leaf == visit)
            _status = visitNodes(*(RegNode* const*)node, _nodeFunc);
    }
}

/*
 * Visits all the descendents of a node in the natural order of their path
 * names.  The node, itself, is not visited.  This function does nothing if
 * "_status" is non-zero.
 *
 * Arguments:
 *      node            Pointer to the node whose descendents are to be visited.
 *                      Shall not be NULL.
 *      func            Pointer to the function to call for each visited node.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success
 *      else            The first non-zero value returned by the function.
 */
static RegStatus visitChildren(
    RegNode* const      node,
    const NodeFunc      func)
{
    if (0 == _status) {
        _nodeFunc = func;

        twalk(node->children, visitNode);
    }

    return _status;
}

/*
 * Frees a node.  This function doesn't free the child-nodes of the node, nor
 * does it remove the node from its parent-node: it only frees resources
 * allocated in the node structure.
 *
 * Arguments:
 *      node            Pointer to the node to be freed.  May be NULL.
 */
static void freeNode(
    RegNode* const      node)
{
    if (NULL != node) {
        freeValues(&node->values);
        freeValues(&node->deletedValues);

        while (NULL != node->children)
            tdelete(*(RegNode**)node->children, &node->children, compareNodes);

        free(node->absPath);
        free(node);
    }
}

/*
 * Marks a single node as being deleted.
 *
 * Arguments:
 *      node            Pointer to the node to be marked.  Shall not be NULL.
 * Returns:
 *      0               Success
 */
static RegStatus markDeleted(
    RegNode* const      node)
{
    node->deleted = 1;

    return 0;
}

/*
 * Marks a node and all its descendents as being deleted.
 *
 * Arguments:
 *      node            Pointer to the node to be marked as deleted along with
 *                      all its descendents.  Shall not be NULL.
 */
static void deleteNodes(
    RegNode* const      node)
{
    (void)visitNodes(node, markDeleted);
}

static RegStatus freeChildren(
    RegNode* const      node);

/*
 * Frees a node and all its descendents.  Removes every freed node from its
 * parent node.
 *
 * Arguments:
 *      node            Pointer to the node to be freed along with all its
 *                      descendents.  Shall not be NULL.
 * Returns:
 *      0               Success
 */
static RegStatus freeNodes(
    RegNode* const      node)
{
    /*
     * The nodes must be freed depth-first.
     */
    RegStatus   status = freeChildren(node);

    if (0 == status) {
        if (NULL != node->parent) {
            (void)tdelete(node, &node->parent->children, compareNodes);

            node->parent = NULL;
        }

        freeNode(node);
    }

    return status;
}

/*
 * Frees all the descendants of a node, but not the node itself.
 *
 * Arguments:
 *      node            Pointer to the node to have its descendants freed.
 *                      Shall not be NULL.
 * Returns:
 *      0               Success
 */
static RegStatus freeChildren(
    RegNode* const      node)
{
    while (node->children != NULL) {
        RegNode* const child = *(RegNode**)node->children; // Can't be NULL
        void* const    ptr = tdelete(child, &node->children, compareNodes);

        log_assert(ptr != NULL); // Root node deletion => unspecified pointer

        freeChildren(child);
        freeNode(child);
    }

    return 0;
}

/*
 * Visits a node and all its descendants in the natural order of their path
 * names.  Does nothing if "_status" is non-zero.  Marks the node as being
 * unmodified upon success.
 *
 * Arguments:
 *      node            Pointer to the node at which to start the traversal.
 *                      Shall not be NULL.
 *      func            Pointer to the function to call at each node.  Shall
 *                      not be NULL.
 * Returns:
 *      0               Success
 *      else            The first non-zero value returned by "func".
 */
static RegStatus visitNodes(
    RegNode* const      node,
    const NodeFunc      func)
{
    if (0 == (_status = func(node)))
        if (0 == (_status = visitChildren(node, func)))
            node->modified = 0;

    return _status;
}

/*
 * Finds a child-node.
 *
 * Arguments:
 *      node            Pointer to the node whose child is to be found.  Shall
 *                      not be NULL.
 *      name            Pointer to the name of the child.  Shall not be NULL.
 * Returns:
 *      NULL            No such child-node exists
 *      else            Pointer to the child-node with the given name
 */
static RegNode* findChild(
    const RegNode*      node,
    const char* const   name)
{
    RegNode     template;
    void*       ptr;

    template.name = name;

    ptr = tfind(&template, &node->children, compareNodes);

    return (NULL == ptr)
        ? NULL
        : *(RegNode**)ptr;
}

/*
 * Finds the node closest to a desired node that is not a descendent of the
 * desired node.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      initPath        Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the node closest to the desired
 *                      node that is not a descendent of the desired node.
 *                      Shall not be NULL.  Set upon successful return.  Can
 *                      point to the desired node.
 *      remPath         Pointer to a pointer to the path name of the desired
 *                      node relative to the returned node.  The client
 *                      should call "free(*remPath)" when this path name is no
 *                      longer needed.
 *      nodeFunc        Pointer to a function to call for each subnode.  May be
 *                      NULL.
 * Returns:
 *      0               Success.  "*node" is set.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus getLastNode(
    RegNode* const      root,
    const char* const   initPath,
    RegNode** const     node,
    char** const        remPath,
    NodeFunc            nodeFunc)
{
    RegStatus   status;

    if (reg_isAbsPath(initPath)) {
        log_add("Invalid relative path name: \"%s\"", initPath);
        status = EINVAL;
    }
    else {
        char*       relPath;

        if (0 == (status = reg_cloneString(&relPath, initPath))) {
            RegNode*    lastNode = root;
            char*       name;
            char*       savePtr;

            for (name = strtok_r(relPath, REG_SEP, &savePtr); NULL != name;
                    name = strtok_r(NULL, REG_SEP, &savePtr)) {
                RegNode*        child = findChild(lastNode, name);

                if (NULL == child)
                    break;

                if (NULL != nodeFunc)
                    (void)nodeFunc(child);

                lastNode = child;
            }

            if (0 == (status = reg_cloneString(remPath,
                (NULL == name)
                    ? ""
                    : initPath + (name - relPath)))) {
                *node = lastNode;
            }

            free(relPath);
        }                               /* "relPath" allocated */
    }

    return status;
}

/*
 * Acts upon the last node given a starting-node and a relative path.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      path            Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the desired node.  May be
 *                      NULL.  Set upon successful return if it's not NULL.
 *      func            Pointer to the function to call when the last node is
 *                      found.
 * Returns:
 *      0               Success.  "*node" is set if and only if "node" is not
 *                      NULL.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus actUponLastNode(
    RegNode* const              root,
    const char* const           path,
    RegNode** const             node,
    RegStatus                   (*const func)(
        const char*                     name,
        RegNode*                        lastNode,
        char*                           savePtr,
        RegNode** const                 node))
{
    RegNode*    lastNode;
    char*       remPath;
    RegStatus   status = getLastNode(root, path, &lastNode, &remPath, NULL);

    if (0 == status) {
        char*   savePtr;
        char*   name = strtok_r(remPath, REG_SEP, &savePtr);

        status = func(name, lastNode, savePtr, node);

        free(remPath);
    }                                   /* "remPath" allocated */

    return status;
}

/*
 * Finds a node.  This function is designed to be called by "actUponLastNode()".
 *
 * Arguments:
 *      name            The last name searched for.  May be NULL, in which case
 *                      "lastNode" points to the desired node.
 *      lastNode        Pointer to the last extant node.  Shall not be NULL.
 *      savePtr         Pointer to the current position in the relative path
 *                      name.
 *      desiredNode     Pointer to a pointer to the desired node.  Shall not be
 *                      NULL.  Set upon successful return.
 * Returns:
 *      0               Success
 *      ENOENT          The desired node wasn't found.
 */
static RegStatus lastFindNode(
    const char* const   name,
    RegNode*            lastNode,
    char*               savePtr,
    RegNode**           desiredNode)
{
    RegStatus   status;

    log_assert(NULL != lastNode);

    if (NULL != name) {
        status = ENOENT;
    }
    else {
        if (NULL != desiredNode)
            *desiredNode = lastNode;
        status = 0;
    }

    return status;
}

/*
 * Finds a node given a starting-node and a relative path.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      path            Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the desired node.  May be NULL.
 *                      Set upon successful return if not NULL.
 * Returns:
 *      0               Success.  "*node" is not NULL iff "node" is not NULL.
 *      ENOENT          No such node exists
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus findNode(
    RegNode* const              root,
    const char* const           path,
    RegNode** const             node)
{
    return actUponLastNode(root, path, node, lastFindNode);
}

/*
 * Undeletes a node.
 *
 * Arguments:
 *      node            Pointer to the node to be undeleted.  Shall not be
 *                      NULL.
 * Returns:
 *      0               Success
 */
static RegStatus undelete(
    RegNode*    node)
{
    node->deleted = 0;

    return 0;
}

/*
 * Ensures that a node in a node-tree exists.  Creates it and any missing
 * ancestors if it doesn't exist.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      path            Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the node.  Shall not be NULL.
 *                      Set upon successful return.
 * Returns:
 *      0               Success.  "*node" is not NULL.
 *      EINVAL          "path" is an invalid relative path name.  "log_add()"
 *                      called.
 *      EEXIST          A node would have to be created with the same absolute
 *                      path name as an existing value.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
static RegStatus ensureNode(
    RegNode* const      root,
    const char* const   path,
    RegNode** const     node)
{
    RegNode*    lastNode;
    char*       remPath;
    RegStatus   status = getLastNode(root, path, &lastNode, &remPath, undelete);

    if (0 == status) {
        char*   name;
        char*   savePtr;

        for (name = strtok_r(remPath, REG_SEP, &savePtr); NULL != name;
                name = strtok_r(NULL, REG_SEP, &savePtr)) {
            RegNode*    child;

            if (0 != (status = newNode(lastNode, name, &child)))
                break;

            lastNode = child;
        }

        if (0 == status)
            *node = lastNode;

        free(remPath);
    }                                   /* "remPath" allocated */

    return status;
}

/*
 * Ensures that a node has not been deleted.
 *
 * Arguments:
 *      node            Pointer to the node to be vetted.  Shall not be NULL.
 * Returns:
 *      0               The node has not been deleted
 *      EPERM           The node has been deleted.  "log_add()" called.
 */
static RegStatus vetExtant(
    const RegNode* const        node)
{
    RegStatus   status;

    if (node->deleted) {
        log_add("Node \"%s\" has been deleted", node->absPath);
        status = EPERM;
    }
    else {
        status = 0;
    }

    return status;
}

/*
 * Puts a value into a node.  If a ValueThing had to be created, then its
 * status is zero; otherwise, its status is unchanged.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.  The client
 *                      may free upon return.
 *      valueThing      Pointer to a pointer to the corresponding ValueThing.
 *                      May be NULL.  Set upon successful return if not NULL.
 * Returns:
 *      0               Success.  "*valueThing" is set if "valueThing" isn't
 *                      NULL.
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node has been deleted.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
static RegStatus putValue(
    RegNode* const      node,
    const char* const   name,
    const char* const   value,
    ValueThing** const  valueThing)
{
    RegStatus   status = vetExtant(node);

    if (0 == status) {
        ValueThing  template;
        ValueThing* vt;
        void*       ptr;

        template.name = (char*)name;        /* safe cast */

        if (NULL != (ptr = tfind(&template, &node->values,
                compareValueThings))) {
            /* The ValueThing already exists.  This should be common. */
            char*   string;

            if (0 == (status = reg_cloneString(&string, value))) {
                vt = *(ValueThing**)ptr;

                free(vt->string);

                vt->string = string;
                status = 0;
            }                               /* "string" allocated */

            log_assert(NULL ==
                tfind(&template, &node->deletedValues, compareValueThings));
        }
        else {
            /*
             * The ValueThing doesn't exist.  This should be uncommon.
             *
             * It's not permitted to have a value and a node with the same
             * absolute path name.
             */
            RegNode     child;

            child.name = name;

            if (NULL != tfind(&child, &node->children, compareNodes)) {
                log_add("A child-node named \"%s\" exists", name);
                status = EEXIST;
            }
            else {
                if (NULL != (ptr = tfind(&template, &node->deletedValues,
                        compareValueThings))) {
                    vt = *(ValueThing**)ptr;

                    (void)tdelete(vt, &node->deletedValues, compareValueThings);
                    freeValueThing(vt);
                }
                if (0 == (status = newValueThing(&vt))) {
                    if (0 == (status = reg_cloneString(&vt->name, name))) {
                        if (NULL == (ptr = tsearch(vt, &node->values,
                                compareValueThings))) {
                            log_syserr("tsearch() failure");
                            status = ENOMEM;
                        }
                        else {
                            status = reg_cloneString(&vt->string, value);
                        }
                    }                       /* "valueThing->name" allocated */

                    if (status)
                        freeValueThing(vt);
                }                           /* "valueThing" allocated */
            }
        }

        if (status) {
            log_add("Couldn't add value \"%s\" to node \"%s\"", name,
                node->absPath);
        }
        else {
            if (NULL != valueThing)
                *valueThing = vt;

            node->modified = 1;
        }
    }

    return status;
}

/*
 * Clears a node.  The values of the node are freed and all descendants of
 * the node are freed.
 *
 * Arguments:
 *      node            Pointer to the node to be cleared.  Shall not be NULL.
 */
static void clear(
    RegNode* const      node)
{
    freeValues(&node->values);
    freeValues(&node->deletedValues);
    freeChildren(node);
}

/*
 * Deletes a value from a node.  Marks both the node and the value as having
 * been modified.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOENT          No such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node has been deleted.  "log_add()" called.
 */
static RegStatus deleteValue(
    RegNode* const      node,
    const char* const   name)
{
    RegStatus   status = vetExtant(node);

    if (0 == status) {
        ValueThing  template;
        void*       ptr;

        template.name = (char*)name;        /* safe cast */

        if (NULL == (ptr = tfind(&template, &node->values,
                compareValueThings))) {
            status = ENOENT;
        }
        else {
            ValueThing*     vt = *(ValueThing**)ptr;

            (void)tdelete(vt, &node->values, compareValueThings);

            if (NULL == tsearch(vt, &node->deletedValues, compareValueThings)) {
                log_syserr("Couldn't add value to set of deleted values");
                status = ENOMEM;
            }
            else {
                node->modified = 1;
                status = 0;
            }
        }
    }

    if (status && ENOENT != status)
        log_add("Couldn't delete value \"%s\" of node \"%s\"",
            name, node->absPath);

    return status;
}

/******************************************************************************
 * Public Functions:
 *
 * No public function calls another public function of this module.
 ******************************************************************************/

/*
 * Allocates a new, empty node suitable for the root of a tree of nodes.
 *
 * Arguments:
 *      node            Pointer to a pointer to the newly-allocated node.  Set
 *                      upon successful return.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus rn_newRoot(
    RegNode** const     node)
{
    return newNode(NULL, "", node);
}

/*
 * Returns the absolute path name of a node.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 * Returns:
 *      Pointer to the absolute path name of the node.  The client shall not
 *      free.
 */
const char* rn_getAbsPath(
    const RegNode* const        node)
{
    return node->absPath;
}

/*
 * Returns the parent node of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose parent node is to be returned.
 *                      Shall not be NULL.
 * Returns:
 *      NULL            The node has no parent node (i.e., the node is a
 *                      root-node)
 *      else            Pointer to the parent node
 */
RegNode* rn_getParent(
    const RegNode* const        node)
{
    return node->parent;
}

/*
 * Returns the name of a node.
 *
 * Arguments:
 *      node            Pointer to the node whose name will be returned.  Shall
 *                      not be NULL.
 * Returns:
 *      Pointer to the name of the node.  The client shall not free.
 */
const char* rn_getName(
    const RegNode* const        node)
{
    return node->name;
}

/*
 * Indicates if a node has been deleted.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 * Returns:
 *      0               The node has not been deleted.
 *      else            The node has been deleted.
 */
int rn_isDeleted(
    const RegNode* const        node)
{
    return node->deleted;
}

/*
 * Puts a value into a node.  If a ValueThing had to be created, then its
 * status is zero; otherwise, its status is unchanged.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 *      value           Pointer to the value.  Shall not be NULL.  The client
 *                      may free upon return.
 *      vt              Pointer to a pointer to the corresponding ValueThing.
 *                      May be NULL.  Set upon successful return if not NULL.
 * Returns:
 *      0               Success.  "*vt" is set if "vt" isn't NULL.
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node has been deleted.  "log_add()" called.
 *      EEXIST          The value would have the same absolute path name as an
 *                      existing node.  "log_add()" called.
 */
RegStatus rn_putValue(
    RegNode* const      node,
    const char* const   name,
    const char* const   value,
    ValueThing** const  vt)
{
    return putValue(node, name, value, vt);
}

/*
 * Returns a value given a starting node and a relative path name.
 *
 * Arguments:
 *      node            Pointer to the node at which to start the search for the
 *                      value.  Shall not be NULL.
 *      path            Pointer to the path name of the value to be returned
 *                      relative to the starting node.  Shall not be NULL.
 *      string          Pointer to a pointer to the string representation of
 *                      the value.  Shall not be NULL.  Set upon successful
 *                      return.  The client should call "free(*string)" when
 *                      the string is no longer needed.
 * Returns:
 *      0               Success.  "*string" is not NULL.
 *      EPERM           The node containing the value has been deleted.
 *                      "log_add()" called.
 *      ENOENT          No such value.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus rn_getValue(
    const RegNode* const        node,
    const char* const           path,
    char** const                string)
{
    RegNode*    lastNode;
    char*       valueName;
    RegStatus   status = getLastNode((RegNode*)node, path, &lastNode,
        &valueName, NULL);              /* safe cast */

    if (0 == status) {
        if (0 == (status = vetExtant(node))) {
            ValueThing  template;
            void*       ptr;

            template.name = (char*)valueName;
            ptr = tfind(&template, &lastNode->values, compareValueThings);

            if (NULL == ptr) {
                log_add("No such value \"%s\" in node \"%s\"", template.name,
                    rn_getAbsPath(lastNode));
                status = ENOENT;
            }
            else {
                status = reg_cloneString(string, (*(ValueThing**)ptr)->string);
            }
        }

        free(valueName);
    }                                   /* "valueName" allocated */

    return status;
}

/*
 * Finds a node given a starting-node and a relative path name.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.  Shall
 *                      not be NULL.
 *      path            Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the desired node.  May be NULL.
 *                      Set upon successful return if not NULL.
 * Returns:
 *      0               Success.  "*node" is set if "node" is not NULL.
 *      ENOENT          No such node exists
 */
RegStatus rn_find(
    RegNode* const      root,
    const char* const   path,
    RegNode** const     node)
{
    return findNode(root, path, node);
}

/*
 * Deletes a value from a node.  The value is only marked as deleted: it is
 * neither removed from the node nor are its resources freed.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 *      name            Pointer to the name of the value.  Shall not be NULL.
 * Returns:
 *      0               Success
 *      ENOENT          The node has no such value
 *      ENOMEM          System error.  "log_add()" called.
 *      EPERM           The node has been deleted.  "log_add()" called.
 */
RegStatus rn_deleteValue(
    RegNode* const      node,
    const char* const   name)
{
    return deleteValue(node, name);
}

/*
 * Ensures that a node in a node-tree exists.  Creates it and any missing
 * ancestors if it doesn't exist.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      path            Pointer to the path name of the node relative to
 *                      the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the node.  Shall not be NULL.
 *                      Set upon successful return.
 * Returns:
 *      0               Success.  "*node" is not NULL.
 *      EEXIST          A node would have to be created with the same absolute
 *                      path name as an existing value.  "log_add()" called.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus rn_ensure(
    RegNode* const      root,
    const char* const   path,
    RegNode** const     node)
{
    return ensureNode(root, path, node);
}

/*
 * Frees a node and all its descendents.
 *
 * Arguments:
 *      node            Pointer to the node to be freed along with all its
 *                      descendents.  May be NULL.
 */
void rn_free(
    RegNode* const      node)
{
    freeNodes(node);
}

/*
 * Finds the node closest to a desired node that is not a descendent of the
 * desired node.
 *
 * Arguments:
 *      root            Pointer to the node at which to start the search.
 *                      Shall not be NULL.
 *      initPath        Pointer to the path name of the desired node relative
 *                      to the starting-node.  Shall not be NULL.
 *      node            Pointer to a pointer to the node closest to the desired
 *                      node that is not a descendent of the desired node.
 *                      Shall not be NULL.  Set upon successful return.  Can
 *                      point to the desired node.
 *      remPath         Pointer to a pointer to the path name of the desired
 *                      node relative to the returned node.  The client
 *                      should call "free(*remPath)" when this path name is no
 *                      longer needed.
 * Returns:
 *      0               Success.  "*node" is set.
 *      ENOMEM          System error.  "log_add()" called.
 */
RegStatus rn_getLastNode(
    RegNode* const              root,
    const char* const           initPath,
    RegNode** const             node,
    char** const                remPath)
{
    return getLastNode(root, initPath, node, remPath, NULL);
}

/*
 * Visits a node and all its descendents in the natural order of their path
 * names.
 *
 * Arguments:
 *      node            Pointer to the node at which to start.  Shall not be
 *                      NULL.
 *      func            Pointer to the function to call at each node.  Shall
 *                      not be NULL.
 * Returns:
 *      0               Success
 *      else            The first non-zero value returned by "func".
 */
RegStatus rn_visitNodes(
    RegNode* const      node,
    const NodeFunc      func)
{
    _status = 0;

    return visitNodes(node, func);
}

/*
 * Visits all the values of a node in the natural order of their names.
 *
 * Arguments:
 *      node            Pointer to the node whose values are to be visited.
 *                      Shall not be NULL.
 *      extant          Pointer to the function to call for each extant value.
 *                      Shall not be NULL.
 *      deleted         Pointer to the function to call for each deleted value.
 *                      May be NULL.
 * Returns:
 *      0               Success
 *      ENOMEM          System error.  "log_add()" called.
 *      else            The first non-zero value returned by "extant" or
 *                      "deleted".
 */
RegStatus rn_visitValues(
    RegNode* const      node,
    const ValueFunc     extant,
    const ValueFunc     deleted)
{
    RegStatus   status;

    _status = 0;
    _valueFunc = extant;

    twalk(node->values, visitValue);

    if (0 == _status && NULL != deleted) {
        _valueFunc = deleted;
        twalk(node->deletedValues, visitValue);
    }

    status = _status;

    return status;
}

/*
 * Frees the deleted values of a node.  This actually removes the values from
 * the node and frees their resources.
 *
 * Arguments:
 *      node            Pointer to the node.  Shall not be NULL.
 */
void rn_freeDeletedValues(
    RegNode* const      node)
{
    freeValues(&node->deletedValues);
}

/*
 * Marks a node and all its descendents as being deleted.  Deleted nodes are
 * only marked as such: they are not actually removed from the node-tree nor
 * are their resources freed.
 *
 * Arguments:
 *      node            Pointer to the node to be deleted along with all its
 *                      descendents.  Shall not be NULL.
 */
void rn_delete(
    RegNode* const      node)
{
    deleteNodes(node);
}

/*
 * Clears a node.  The values of the node are freed and all descendents of
 * the node are freed.
 *
 * Arguments:
 *      node            Pointer to the node to be cleared.  Shall not be NULL.
 */
void rn_clear(
    RegNode* const      node)
{
    clear(node);
}

/*
 * Sets the status of a ValueThing.  Returns the previous status.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing to have its status set.
 *                      Shall not be NULL.
 *      status          The status for the ValueThing.
 * Returns:
 *      The previous value of the status.
 */
int vt_setStatus(
    ValueThing* const   vt,
    const int           status)
{
    int prev = vt->status;

    vt->status = status;

    return prev;
}

/*
 * Returns the status of a ValueThing.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing.  Shall not be NULL.
 * Returns:
 *      The status of the ValueThing.
 */
int vt_getStatus(
    const ValueThing* const   vt)
{
    return vt->status;
}

/*
 * Returns the name of a ValueThing.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing.  Shall not be NULL.
 * Returns:
 *      Pointer to the name of the ValueThing.  The Client shall not free.
 */
const char* vt_getName(
    const ValueThing* const   vt)
{
    return vt->name;
}

/*
 * Returns the string-value of a ValueThing.
 *
 * Arguments:
 *      vt              Pointer to the ValueThing.  Shall not be NULL.
 * Returns:
 *      Pointer to the string-value of the ValueThing.  The Client shall not
 *      free.
 */
const char* vt_getValue(
    const ValueThing* const     vt)
{
    return vt->string;
}

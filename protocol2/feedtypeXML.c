/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: feedtypeXML.c,v 1.1.2.1 2009/08/14 14:57:16 steve Exp $ */

/*
 * This module parses XML-formatted feedtype definitions from a file-descriptor
 * and populates a feedtype database with those definitions.
 */

#ifdef UNIT_TEST
#  undef NDEBUG
#endif

#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <search.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atofeedt.h"
#include "error.h"
#include "feedtypeDB.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "ldm_clnt_misc.h"
#include "rpcutil.h"
#include "log.h"
#include "xmlparse.h"

#include "feedtypeXML.h"                /* to ensure consistency */

typedef struct FaEntry  FaEntry;


typedef struct StackElt {
    const FaEntry*      fa;
    void*               data;
    struct StackElt*    next;
    const FaEntry*      assocFaEntry;
} StackElt;

/*
 * "User Data" for the XML parser.
 */
typedef struct UserData {
    XML_Parser          parser;
    FeedtypeDB*         db;
    StackElt*           stack;
    FeedtypeXmlError    errorCode;
    int                 unknownDepth;
} UserData;

/*
 * An entry in the finite-automata table for parsing XML:
 */
struct FaEntry {
    const char*         name;
    const FaEntry*      nextFa;
    ErrorObj* (*                startTagHandler)(void**, void*);
    ErrorObj* (*                textHandler)(void*, const XML_Char*, int);
    ErrorObj* (*                endTagHandler)(void*, void*, UserData*);
};

typedef struct NameElt {
    char*               name;
    struct NameElt*     next;
} NameElt;

typedef struct {
    const char*         name;
    NameElt*            include;
    NameElt*            exclude;
} Def;

typedef struct {
    char*               name;
    int                 bit;
} BitDef;

typedef struct {
    char*               name;
    NameElt*            include;
    NameElt*            exclude;
} MaskDef;

typedef struct {
    char*               name;
    NameElt*            include;
    NameElt*            exclude;
    feedtypet           value;
} ValueDef;

typedef struct {
    char*       text;
    size_t      len;
    size_t      max;
} TextBuffer;


static void
freeNameList(
    NameElt*    elt)
{
    while (elt) {
        NameElt*        next = elt->next;

        free(elt->name);
        free(elt);
        elt = next;
    }
}


static ErrorObj*
newTextBuffer(
    TextBuffer** const  data)
{
    ErrorObj*   error;
    TextBuffer* textBuf = (TextBuffer*)malloc(sizeof(TextBuffer));

    assert(data);

    if (NULL == textBuf) {
        error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu bytes for TextBuffer: %s",
            (unsigned long)sizeof(TextBuffer), strerror(errno));
    }
    else {
        textBuf->text = "";
        textBuf->len = 0;
        textBuf->max = 0;
        *data = textBuf;
        error = NULL;
    }

    return error;
}


static void
freeTextBuffer(
    TextBuffer* const   textBuf)
{
    if (textBuf) {
        if (textBuf->max)
            free(textBuf->text);
        free(textBuf);
    }
}


static ErrorObj*
handleText(
    void* const         data,
    const XML_Char*     text,
    int                 len)
{
    ErrorObj*   error;
    TextBuffer* textBuf = (TextBuffer*)data;

    assert(text);
    assert(textBuf);

    if (0 >= len) {
        error = NULL;
    }
    else {
        size_t  totlen = textBuf->len + len;

        if (totlen <= textBuf->max) {
            error = NULL;
        }
        else {
            size_t      newMax = (totlen * 1.618034) + 0.5;
            char*       buf =
                (char*)realloc(textBuf->max ? textBuf->text : NULL, newMax+1);

            if (!buf) {
                error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
                    "Couldn't allocate %lu bytes for a text buffer: %s",
                    newMax+1, strerror(errno));
            }
            else {
                textBuf->text = buf;
                (void)memcpy(textBuf->text+textBuf->len, text, len);
                textBuf->len += len;
                textBuf->text[textBuf->len] = 0;
                textBuf->max = newMax;
                error = NULL;
            }
        }
    }

    return error;
}


static ErrorObj*
startBitDef(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    BitDef*     bitDef = (BitDef*)malloc(sizeof(BitDef));

    assert(data);

    if (!bitDef) {
        error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu bytes for a BitDef",
            (unsigned long)sizeof(BitDef), strerror(errno));
    }
    else {
        bitDef->name = NULL;
        bitDef->bit = -1;
        *data = bitDef;
        error = NULL;
    }

    return error;
}


static ErrorObj*
endBitDef(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*   error;
    BitDef*     bitDef = (BitDef*)data;

    assert(bitDef);
    assert(userData);

    if (!bitDef->name) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL, "<bitdef> <name> not specified");
    }
    else if (-1 == bitDef->bit) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL, "<bitdef> <bit> not specified");
    }
    else {
        error = fdb_add_bit(userData->db, bitDef->name, bitDef->bit, 0);

        if (error)
            error = ERR_NEW(FX_DATABASE_ERROR, error, NULL);
    }

    free(bitDef->name);
    free(bitDef);

    return error;
}


static ErrorObj*
startDefName(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    Def*        def = (Def*)container;

    assert(def);

    if (NULL != def->name) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL, "Attempt to respecify <name>");
    }
    else {
        error = newTextBuffer((TextBuffer**)data);
    }

    return error;
}


static ErrorObj*
endDefName(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*   error;
    TextBuffer* textBuf = (TextBuffer*)data;
    Def*        def = (Def*)container;

    assert(textBuf);
    assert(def);

    if (0 == textBuf->len) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL, "Empty <name> in feedtype "
            "definition");
    }
    else {
        def->name = textBuf->text;
        error = NULL;
    }

    free(data);

    return error;
}


static ErrorObj*
startBitDefBit(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    BitDef*     bitDef = (BitDef*)container;

    assert(bitDef);

    if (-1 != bitDef->bit) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL,
            "Attempt to respecify <bitdef>'s <bit>");
    }
    else {
        error = newTextBuffer((TextBuffer**)data);
    }

    return error;
}


static ErrorObj*
endBitDefBit(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*   error;
    TextBuffer* textBuf = (TextBuffer*)data;
    BitDef*     bitDef = (BitDef*)container;
    long        bit;

    assert(textBuf);
    assert(bitDef);

    errno = 0;
    bit = strtol(textBuf->text, NULL, 0);

    if (errno || bit < 0 || bit > 31) {
        error = ERR_NEW1(FX_PARSE_ERROR, NULL,
            "Illegal bit-index \"%s\"", textBuf->text);
    }
    else {
        bitDef->bit = bit;
        error = NULL;
    }

    freeTextBuffer(textBuf);

    return error;
}


static ErrorObj*
startMaskDef(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    MaskDef*    maskDef = (MaskDef*)malloc(sizeof(MaskDef));

    assert(data);

    if (!maskDef) {
        error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu bytes for a MaskDef",
            (unsigned long)sizeof(BitDef), strerror(errno));
    }
    else {
        maskDef->name = NULL;
        maskDef->include = NULL;
        maskDef->exclude = NULL;
        *data = maskDef;
        error = NULL;
    }

    return error;
}


static ErrorObj*
startDefIncludeExclude(
    void** const                data,
    const NameElt* const        list,
    const char* const           type)
{
    ErrorObj*   error;

    assert(data);
    assert(type);

    if (list) {
        error = ERR_NEW1(FX_PARSE_ERROR, NULL,
            "Attempt to redefine %s-list", type);
    }
    else {
        error = newTextBuffer((TextBuffer**)data);
    }

    return error;
}


static ErrorObj*
startDefInclude(
    void** const        data,
    void* const         container)
{
    Def*        def = (Def*)container;

    return startDefIncludeExclude(data, def->include, "include");
}


static ErrorObj*
startDefExclude(
    void** const        data,
    void* const         container)
{
    Def*        def = (Def*)container;

    return startDefIncludeExclude(data, def->exclude, "exclude");
}


static ErrorObj*
endDefIncludeExclude(
    TextBuffer* const   textBuf,
    NameElt** const     list)
{
    ErrorObj*   error;
    char*       cp;

    assert(textBuf);
    assert(list);

    cp = textBuf->text;

    for (error = NULL; *cp && !error; ) {
        static const char*      whiteSpace = " \t\n\r";
        size_t                  len = strspn(cp, whiteSpace);

        if (len)
            cp += len;

        len = strcspn(cp, whiteSpace);

        if (len) {
            NameElt*    nameElt = (NameElt*)malloc(sizeof(NameElt));

            if (!nameElt) {
                error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
                    "Couldn't allocate %lu bytes for a NameElt: %s",
                    (unsigned long)sizeof(NameElt), strerror(errno));
            }
            else {
                char*   name = (char*)malloc(len+1);

                if (!name) {
                    error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
                        "Couldn't allocate %lu bytes for a name buffer: %s",
                        (unsigned long)sizeof(NameElt), strerror(errno));
                }
                else {
                    (void)strncpy(name, cp, len);

                    name[len] = '\0';
                    nameElt->name = name;
                    nameElt->next = *list;
                    *list = nameElt;
                    cp += len;
                }
            }                                   /* name buffer allocated */
        }                                       /* found blackspace */
    }                                           /* name loop */

    freeTextBuffer(textBuf);

    return error;
}


static ErrorObj*
endDefInclude(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    TextBuffer* textBuf = (TextBuffer*)data;
    Def*        def = (Def*)container;

    assert(def);

    return endDefIncludeExclude(textBuf, &def->include);
}


static ErrorObj*
endDefExclude(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    TextBuffer* textBuf = (TextBuffer*)data;
    Def*        def = (Def*)container;

    assert(def);

    return endDefIncludeExclude(textBuf, &def->exclude);
}


static ErrorObj*
endMaskDef(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*   error;
    MaskDef*    maskDef = (MaskDef*)data;

    assert(maskDef);
    assert(userData);

    if (NULL == maskDef->name) {
        error =
            ERR_NEW(FX_PARSE_ERROR, NULL, "<maskdef>'s <name> not specified");
    }
    else if (NULL == maskDef->include && NULL == maskDef->exclude) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL,
            "Neither <maskdef>'s <include> nor <exclude> specified");
    }
    else {
        MaskEntry*      entry;
        NameElt*        elt;

        error = me_new(maskDef->name, &entry);

        if (!error) {
            for (elt = maskDef->include; !error && elt; elt = elt->next)
                error = me_include(entry, elt->name);

            for (elt = maskDef->exclude; !error && elt; elt = elt->next)
                error = me_exclude(entry, elt->name);

            if (!error)
                error = me_add(entry, 0);

            if (error)
                me_free(entry);
        }

        if (error)
            error = ERR_NEW(FX_DATABASE_ERROR, error, NULL);
    }

    freeNameList(maskDef->include);
    freeNameList(maskDef->exclude);
    free(maskDef->name);
    free(maskDef);

    return error;
}


static ErrorObj*
startValueDef(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    ValueDef*   valueDef = (ValueDef*)malloc(sizeof(ValueDef));

    assert(data);

    if (!valueDef) {
        error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
            "Couldn't allocate %lu bytes for a ValueDef",
            (unsigned long)sizeof(BitDef), strerror(errno));
    }
    else {
        valueDef->name = NULL;
        valueDef->include = NULL;
        valueDef->exclude = NULL;
        valueDef->value = 0;
        *data = valueDef;
        error = NULL;
    }

    return error;
}


static ErrorObj*
startValueDefValue(
    void** const        data,
    void* const         container)
{
    ErrorObj*   error;
    ValueDef*   valueDef = (ValueDef*)container;

    assert(valueDef);

    if (0 != valueDef->value) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL,
            "Attempt to respecify <valuedef>'s <value>");
    }
    else {
        error = newTextBuffer((TextBuffer**)data);
    }

    return error;
}


static ErrorObj*
endValueDefValue(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*           error;
    TextBuffer*         textBuf = (TextBuffer*)data;
    ValueDef*           valueDef = (ValueDef*)container;
    unsigned long       value;

    assert(textBuf);
    assert(valueDef);

    errno = 0;
    value = strtoul(textBuf->text, NULL, 0);

    if (errno || value > ANY) {
        error = ERR_NEW1(FX_PARSE_ERROR, NULL,
            "Illegal feedtype value \"%s\"", textBuf->text);
    }
    else {
        valueDef->value = (feedtypet)value;
        error = NULL;
    }

    freeTextBuffer(textBuf);

    return error;
}


static ErrorObj*
endValueDef(
    void* const         data,
    void* const         container,
    UserData* const     userData)
{
    ErrorObj*   error;
    ValueDef*   valueDef = (ValueDef*)data;

    assert(valueDef);
    assert(userData);

    if (NULL == valueDef->name) {
        error =
            ERR_NEW(FX_PARSE_ERROR, NULL, "<valuedef>'s <name> not specified");
    }
    else if (0 == valueDef->value) {
        error = ERR_NEW(FX_PARSE_ERROR, NULL,
            "<valuedef>'s <value> not specified");
    }
    else {
        ValueEntry*     entry;
        NameElt*        elt;

        error = ve_new(userData->db, valueDef->name, valueDef->value,
            &entry, 0);

        if (!error) {
            for (elt = valueDef->include; !error && elt; elt = elt->next)
                error = ve_include(entry, elt->name);

            for (elt = valueDef->exclude; !error && elt; elt = elt->next)
                error = ve_exclude(entry, elt->name);

            if (!error)
                error = ve_add(entry, 0);

            if (error)
                ve_free(entry);
        }

        if (error)
            error = ERR_NEW(FX_DATABASE_ERROR, error, "");
    }

    freeNameList(valueDef->include);
    freeNameList(valueDef->exclude);
    free(valueDef->name);
    free(valueDef);

    return error;
}


static void
startElement(
    void*               data,
    const XML_Char*     eltName,
    const XML_Char**    attrs)
{
    UserData*           userData = (UserData*)data;

    assert(userData);

    if (userData->unknownDepth) {
        userData->unknownDepth++;
    }
    else {
        const FaEntry*          faEntry = userData->stack->fa;

        for (; faEntry->name; faEntry++)
            if (strcasecmp(eltName, faEntry->name) == 0)
                break;

        if (!faEntry->name) {
            userData->unknownDepth++;
        }
        else {
            ErrorObj*   error;
            StackElt*   newElt = (StackElt*)malloc(sizeof(StackElt));

            if (!newElt) {
                error = ERR_NEW2(FX_SYSTEM_ERROR, NULL,
                    "Couldn't allocate %lu bytes for a StackElt: %s",
                    (unsigned long)sizeof(StackElt), strerror(errno));
            }
            else {
                newElt->fa = faEntry->nextFa;
                newElt->assocFaEntry = faEntry;
                newElt->next = userData->stack;

                if (!faEntry->startTagHandler) {
                    userData->stack = newElt;
                    error = NULL;
                }
                else {
                    error = (*faEntry->startTagHandler)
                        (&newElt->data, userData->stack->data);

                    if (!error) {
                        userData->stack = newElt;
                    }
                    else {
                        /*
                         * Because an error occurred, skip the remainder of this
                         * XML element.
                         */
                        free(newElt);
                        userData->unknownDepth++;
                    }
                }
            }                           /* stack-element allocated */

            if (error) {
                userData->errorCode = err_code(error);
                err_log_and_free(
                    ERR_NEW2(
                        err_code(error),
                        error,
                        "Failure at line %d, character %d of XML input",
                        XML_GetCurrentLineNumber(userData->parser),
                        XML_GetCurrentColumnNumber(userData->parser)),
                    ERR_ERROR);
            }
        }                                       /* element name found */
    }                                           /* not in unknown element */
}


static void
textHandler(
    void*               data,
    const XML_Char*     text,
    int                 len)
{
    UserData*           userData = (UserData*)data;

    assert(userData);

    if (!userData->unknownDepth) {
        const FaEntry*  faEntry = userData->stack->assocFaEntry;

        if (faEntry->textHandler) {
            ErrorObj*   error;

            error = (*faEntry->textHandler)
                (userData->stack->data, text, len);

            if (error) {
                userData->errorCode = err_code(error);
                err_log_and_free(
                    ERR_NEW2(
                        err_code(error),
                        error,
                        "Failure at line %d, character %d of XML input",
                        XML_GetCurrentLineNumber(userData->parser),
                        XML_GetCurrentColumnNumber(userData->parser)),
                    ERR_ERROR);
            }
        }
    }
}


static void
endElement(
    void*               data,
    const XML_Char*     eltName)
{
    UserData*           userData = (UserData*)data;

    assert(userData);

    if (userData->unknownDepth) {
        --userData->unknownDepth;
    }
    else {
        ErrorObj*       error;
        StackElt* const endingStackElt = userData->stack;
        const FaEntry*  assocFaEntry;

        assert(endingStackElt);

        assocFaEntry = endingStackElt->assocFaEntry;

        if (strcasecmp(assocFaEntry->name, eltName)) {
            error = ERR_NEW1(FX_PARSE_ERROR, NULL,
                "Unexpected end-tag \"%s\"", eltName);
        }
        else {
            StackElt* const     currElt = endingStackElt->next;

            if (!assocFaEntry->endTagHandler) {
                error = NULL;
            }
            else {
                error = (*assocFaEntry->endTagHandler)
                    (endingStackElt->data, currElt->data, userData);
            }

            userData->stack = currElt;
            free(endingStackElt);
        }

        if (error) {
            userData->errorCode = err_code(error);
            err_log_and_free(
                ERR_NEW2(err_code(error), error,
                    "Failure at line %d, character %d of XML input",
                    XML_GetCurrentLineNumber(userData->parser),
                    XML_GetCurrentColumnNumber(userData->parser)),
                ERR_ERROR);
        }
    }                                   /* in known element */
}


static size_t
getPageSize()
{
    long        size = sysconf(_SC_PAGESIZE);

    return
        -1 != size
            ? (size_t)size
            : 8192;     /* must return something */
}


/*******************************************************************************
 * Begin Public Interface
 ******************************************************************************/


/*
 * Parses XML from a file-descriptor for feedtype definitions and places the
 * definitions in a feedtype database.  Returns when the end-of-file is
 * encountered or an error occurs.
 *
 * Arguments:
 *      *fd             The file-descriptor from which to read the XML-encoded
 *                      feedtype definitions.  This function will not close
 *                      the file-descriptor.
 *      *db             The feedtype database to be populated.
 *
 * Returns:
 *      FX_SUCCESS (=0)         Success
 *      FX_SYSTEM_ERROR         System error
 *      FX_DATABASE_ERROR       Error populating database
 *      FX_PARSE_ERROR          Couldn't parse XML
 */
int
fx_parse_fd(
    const int           fd,
    FeedtypeDB* const   db)
{
    FeedtypeXmlError            errorCode = FX_SUCCESS;         /* default */
    int                         bytes_read;
    size_t                      pageSize = getPageSize();
    StackElt*                   stackElt;
    static const FaEntry        bitDefFa[] = {
        {"name", NULL, startDefName, handleText, endDefName},
        {"bit", NULL, startBitDefBit, handleText, endBitDefBit},
        {NULL}
    };
    static const FaEntry        maskDefFa[] = {
        {"name", NULL, startDefName, handleText, endDefName},
        {"include", NULL, startDefInclude, handleText, endDefInclude},
        {"exclude", NULL, startDefExclude, handleText, endDefExclude},
        {NULL}
    };
    static const FaEntry        valueDefFa[] = {
        {"name", NULL, startDefName, handleText, endDefName},
        {"value", NULL, startValueDefValue, handleText, endValueDefValue},
        {"include", NULL, startDefInclude, handleText, endDefInclude},
        {"exclude", NULL, startDefExclude, handleText, endDefExclude},
        {NULL}
    };
    static const FaEntry        definitionsFa[] = {
        {"bitDef", bitDefFa, startBitDef, NULL, endBitDef},
        {"maskDef", maskDefFa, startMaskDef, NULL, endMaskDef},
        {"valueDef", valueDefFa, startValueDef, NULL, endValueDef},
        {NULL}
    };
    static const FaEntry        startFa[] = {
        {"definitions", definitionsFa, NULL, NULL, NULL},
        {NULL}
    };

    assert(db);
    assert(0 <= fd);

    stackElt = (StackElt*)malloc(sizeof(StackElt));

    if (NULL == stackElt) {
        err_log_and_free(
            ERR_NEW2(
                FX_SYSTEM_ERROR,
                NULL,
                "Couldn't allocate %lu bytes for a StackElt: %s",
                (unsigned long)sizeof(StackElt), strerror(errno)),
            ERR_ERROR);
        errorCode = FX_SYSTEM_ERROR;
    }
    else {
        static const FaEntry    nilFa[] = {{NULL}};
        XML_Parser              parser = XML_ParserCreate("ISO-8859-1");
        UserData                userData;

        userData.db = db;
        userData.errorCode = FX_SUCCESS;
        userData.unknownDepth = 0;

        stackElt->fa = startFa;
        stackElt->data = NULL;
        stackElt->next = NULL;
        stackElt->assocFaEntry = nilFa;
        userData.stack = stackElt;
        userData.parser = parser;

        XML_SetElementHandler(parser, startElement, endElement);
        XML_SetCharacterDataHandler(parser, textHandler);
        XML_SetUserData(parser, &userData);

        do {
            void*  buf = XML_GetBuffer(parser, pageSize);

            if (NULL == buf) {
                err_log_and_free(
                    ERR_NEW1(
                        FX_SYSTEM_ERROR,
                        NULL,
                        "Couldn't get buffer for parsing XML: %s",
                        XML_ErrorString(XML_GetErrorCode(parser))),
                    ERR_ERROR);
                errorCode = FX_SYSTEM_ERROR;
            }
            else {
                bytes_read = read(fd, buf, pageSize);

                if (-1 == bytes_read) {
                    err_log_and_free(
                        ERR_NEW2(
                            FX_SYSTEM_ERROR,
                            NULL,
                            "Error reading %lu bytes of XML: %s",
                            (unsigned long)pageSize,
                            strerror(errno)), 
                        ERR_ERROR);
                    errorCode = FX_SYSTEM_ERROR;
                }
                else if (!XML_ParseBuffer(parser, bytes_read,
                        0 == bytes_read)) {
                    if (userData.errorCode) {
                        errorCode = userData.errorCode;
                    }
                    else {
                        err_log_and_free(
                            ERR_NEW3(
                                FX_PARSE_ERROR,
                                NULL,
                                "%s: Failure at line %d, character %d of XML "
                                    "input",
                                XML_ErrorString(XML_GetErrorCode(parser)),
                                XML_GetCurrentLineNumber(userData.parser),
                                XML_GetCurrentColumnNumber(userData.parser)),
                            ERR_ERROR);
                        errorCode = FX_PARSE_ERROR;
                    }
                }
            }                           /* got XML buffer */
        } while (!errorCode && userData.stack->fa != startFa && bytes_read);

        if (!errorCode && userData.stack->fa != startFa) {
            err_log_and_free(
                ERR_NEW(
                    FX_PARSE_ERROR,
                    NULL,
                    "Ill-formed XML: missing end tag(s)"),
                ERR_ERROR);
            errorCode = FX_PARSE_ERROR;
        }

        XML_ParserFree(parser);
    }                                   /* initial stack-element allocated */

    return errorCode;
}


/*
 * Parses XML from a file for feedtype definitions and places the
 * definitions in a feedtype database.  Returns when the end-of-file is
 * encountered or an error occurs.
 *
 * Arguments:
 *      pathname        The pathname of the file from which to read the 
 *                      XML-encoded feedtype definitions.
 *      *db             The feedtype database to be populated.
 *
 * Returns:
 *      FX_SUCCESS (=0)         Success
 *      FX_OPEN_ERROR           Couldn't open the given file.
 *      FX_SYSTEM_ERROR         System error
 *      FX_DATABASE_ERROR       Error populating database
 *      FX_PARSE_ERROR          Couldn't parse XML
 */
int
fx_parse_file(
    const char* const   pathname,
    FeedtypeDB* const   db)
{
    FeedtypeXmlError            errorCode;
    int                         fd = open(pathname, O_RDONLY);

    if (-1 != fd) {
        errorCode = fx_parse_fd(fd, db);
        (void)close(fd);
    }
    else {
        err_log_and_free(
            ERR_NEW2(0, NULL, "Couldn't open file \"%s\": %s",
                pathname, strerror(errno)),
            ERR_ERROR);
        errorCode = FX_OPEN_ERROR;
    }

    return errorCode;
}


#ifdef UNIT_TEST

int
main()
{
    int status;

    if (-1 == openulog("feedtypeXML", 0, LOG_LDM, "-")) {
        (void)fprintf(stderr, "Couldn't initialize logging");
        status = 1;
    }
    else {
        static const char* const        pathname = "feedtypes.xml";
        FeedtypeDB*                     db;
        ErrorObj*                       error = fdb_new(&db);

        if (error) {
            err_log_and_free(error, ERR_ERROR);
            status = 2;
        }
        else {
            if (fx_parse_file(pathname, db)) {
                err_log_and_free(
                    ERR_NEW1(0, NULL, "Couldn't get feedtype definitions "
                        "from file \"%s\"", pathname),
                    ERR_ERROR);
                status = 3;
            }
            else {
                status = 0;
            }

            fdb_free(db);
        }                                       /* feedtype database created */

        closeulog();
    }                                           /* logging enabled */

    return status;
}

#endif

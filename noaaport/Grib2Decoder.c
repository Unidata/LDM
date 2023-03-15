/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file Grib2Parser.c
 *
 * This file defines the API for parsing a GRIB edition 2 message.
 *
 * Functions in this module are thread-compatible but not thread-safe.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"
#include "Grib2Decoder.h"
#include "grib2.h"
#include "log.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 * Expandable list of fixed-size elements:
 */
typedef struct {
    char*  elts;    /**< Pointer to element array */
    size_t eltSize; /**< Size of an element in bytes */
    size_t max;     /**< Size of element array */
    size_t num;     /**< Number of elements in array */
}       List;

/**
 * Section 0:
 */
typedef struct {
    g2int       discipline; /**< Discipline - GRIB Master Table Number (see Code
                                 Table 0.0). */
    g2int       edition;    /**< GRIB Edition Number. */
    g2int       len;        /**< Length of GRIB message in bytes. */
}       Section0;

/**
 * Decoded section.
 */
struct Grib2Section {
    const unsigned char* buf;
    size_t               len;
    unsigned             type;
};

/**
 * Decoded GRIB-2 message:
 */
struct DecodedGrib2Msg {
    const unsigned char* buf;      /**< Pointer to buffer */
    size_t               bufLen;   /**< Length of buffer in bytes */
    Grib2Section*        sec1;     /**< Pointer to Section 1 */
    Section0             sec0;     /**< Section 0 */
    List                 sections; /**< List of sections in the order in which
                                        they appear in the encoded GRIB-2
                                        message */
    List                 fields;   /**< Fields in the GRIB-2 message */
};

/**
 * Sections associated with a GRIB-2 field:
 */
#define G2F_MIN_SEC 3
#define G2F_MAX_SEC 7
#define G2F_NUM_SEC 5
typedef Grib2Section (*FieldSections[G2F_NUM_SEC]);

/**
 * GRIB-2 field:
 */
struct Grib2Field {
    DecodedGrib2Msg* decoded;               /**< Pointer to enclosing
                                                 decoded GRIB-2 message */
    Grib2Section*    sections[G2F_NUM_SEC]; /**< Associated sections in order */
};

/**
 * Initializes a list.
 *
 * Atomic
 * Not idempotent,
 * Thread-safe
 *
 * @param[out] list              Pointer to list.
 * @param[in]  eltSize           Size of an element in bytes.
 * @param[in]  initSize          Initial size of list.
 * @retval     0                 Success.
 * @retval     G2D_SYSERR        System error.
 */
static int list_init(
    List* const    list,
    const unsigned eltSize,
    const unsigned initSize)
{
    void* const elts = (void*)log_malloc(eltSize*initSize, "list");

    if (elts == NULL)
        return G2D_SYSERR;

    list->elts = elts;
    list->eltSize = eltSize;
    list->max = initSize;
    list->num = 0;

    return 0;
}

/**
 * Clears a list.
 *
 * Atomic
 * Not idempotent,
 * Thread-safe
 *
 * @param[in] list              Pointer to the list.
 */
static void list_clear(
    List* const list)
{
    free(list->elts);
}

/**
 * Appends an element to an expandable list of fixed-size elements.
 *
 * Atomic
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] list              Pointer to the list of sections.
 * @param[in]     elt               The element to append. The size of the
 *                                  element must equal the size argument passed
 *                                  to @codelist_init()}.
 * @retval        0                 Success.
 * @retval        G2D_SYSERR        System error.
 */
static int list_append(
    List* const   list,
    void* const   elt)
{
    while (list->num >= list->max) {
        const unsigned        newMax = 2 * list->max;
        size_t                nbytes = newMax * list->eltSize;
        void* const           newElts = (void*)realloc(list->elts, nbytes);

        if (0 == newElts) {
            log_add("Couldn't increase list to %lu bytes", nbytes);
            return G2D_SYSERR;
        }

        list->elts = newElts;
        list->max = newMax;
    }

    (void)memcpy(list->elts+list->eltSize*list->num++, elt, list->eltSize);

    return 0;
}

/**
 * Returns the number of elements in a list.
 *
 * Atomic
 * Idempotent,
 * Thread-safe
 *
 * @param[in] list  Pointer to the list.
 * @return          The number of sections in the list.
 */
static size_t list_getSize(
    const List* const list)
{
    return list->num;
}

/**
 * Returns a pointer to an element in a list
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in] list  Pointer to the list.
 * @param[in] i     Index of the element to be returned.
 * @retval    NULL  Index is out-of-bounds.
 * @return          Pointer to element corresponding to given index.
 */
static void* list_getElement(
    const List* const list,
    const size_t      i)
{
    return (i < 0 || i >= list->num) ? NULL : list->elts + i*list->eltSize;
}

/**
 * Initializes a decoded section 0 from an encoded section 0.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] sec0        Pointer to decoded section 0.
 * @param[out] secLen      Pointer to length of section 0.
 * @param[in]  buf         Pointer to start of encoded section 0.
 * @param[in]  len         Length of buffer in bytes.
 * @retval     0           Success. \c *sec0 and \c *secLen are set.
 * @retval     G2D_INVALID Invalid section 0.
 * @retval     G2D_NOT_2   Not GRIB edition 2.
 */
static int sec0_init(
    Section0* const            sec0,
    size_t* const              secLen,
    const unsigned char* const buf,
    const size_t               len)
{
    g2int discipline;
    g2int edition;
    g2int msgLen;

    if (len < 16) {
        log_add("GRIB-2 message length less than 16 bytes: %lu",
                (unsigned long)len);
        return G2D_INVALID;
    }

    if (buf[0] != 'G' || buf[1] != 'R' || buf[2] != 'I' || buf[3] != 'B') {
        log_add("GRIB-2 message doesn't start with \"GRIB\"");
        return G2D_INVALID;
    }

    gbit(buf+6, &discipline, 0, CHAR_BIT);
    gbit(buf+7, &edition, 0, CHAR_BIT);

    if (2 != edition) {
        log_add("GRIB message isn't edition 2: %ld", (long)edition);
        return G2D_NOT_2;
    }

    gbit(buf+12, &msgLen, 0, 4*CHAR_BIT);

    if (len < msgLen) {
        log_add("Stated GRIB-2 message-length longer than actual message: "
                "stated=%ld; actual= %lu", (long)msgLen, (unsigned long)len);
        return G2D_INVALID;
    }

    sec0->discipline = discipline;
    sec0->edition = edition;
    sec0->len = msgLen;
    *secLen = 16;

    return 0;
}

/**
 * Clears a section 0.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in,out] sec0  Pointer to section 0.
 */
static void sec0_clear(
    Section0* const sec0)
{
}

/**
 * Validates a decoded section 1.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] sec              Pointer to section 1.
 * @retval     0                Success. \c *sec is validated.
 * @retval     G2D_INVALID      Invalid section 1.
 */
static int sec1_validate(
    Grib2Section* const       sec)
{
    if (sec->len < 21) {
        log_add("Section 1 less than 21 bytes: ", (unsigned long)sec->len);
        return G2D_INVALID;
    }

    return 0;
}

/**
 * Trivially validate a decoded section.
 *
 * @param[in] section  Pointer to the decoded section.
 * @return    0        Always.
 */
static int sec_validate(
    Grib2Section* const       sec)
{
    return 0;
}

#define SEC_MIN_TYPE  1  /**< Minimum valid section type */
#define SEC_MAX_TYPE  7  /**< Maximum valid section type */
#define SEC_NUM_TYPES (SEC_MAX_TYPE - SEC_MIN_TYPE + 1)

/**
 * Indicates if the type of a section is valid.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in] secType  The type of the section.
 * @retval    0        if and only if the type is not valid.
 */
static int g2s_isValid(
    const g2int secType)
{
    return secType >= SEC_MIN_TYPE && secType <= SEC_MAX_TYPE;
}

/**
 * Validates a decoded section.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] section           Address of pointer to decoded section.
 * @retval     0                 Success. \c *section is set.
 * @retval     G2D_INVALID       Invalid section.
 */
static int g2s_validate(
    Grib2Section* const section)
{
    static int   (*funcs[SEC_NUM_TYPES])(Grib2Section*) = {
        sec1_validate,
        sec_validate,
        sec_validate,
        sec_validate,
        sec_validate,
        sec_validate,
        sec_validate
    };
    int        status;
    int        i = section->type - SEC_MIN_TYPE;

    if ((status = funcs[i](section)))
        return status;

    return 0;
}

/**
 * Indicates if an encoded section is actually the last section sentinel.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  buf              Pointer to start of encoded section.
 * @param[in]  bufLen           Length of buffer in bytes.
 * @retval     0                Section is not last section.
 * @retval     G2D_INVALID      Section is invalid.
 * @retval     G2D_END          section is last section (i.e., "7777" sentinel
 *                              encountered).
 */
static int g2s_isLastSection(
    const unsigned char* const  buf,
    const size_t                bufLen)
{
    if (bufLen < 4) {
        log_add("Remaining GRIB-2 message too short to contain a last "
                "section: %lu bytes remaining", (unsigned long)bufLen);
        return G2D_INVALID;
    }

    if (buf[0] == '7' && buf[1] == '7' && buf[2] == '7' && buf[3] == '7')
        return G2D_END;

    return 0;
}

/**
 * Decodes and validates the length and section-type parameters of an encoded
 * section. For a section to be valid, the following must be true:
 *
 *      * The length parameter of the section must be less than or equal to the
 *        length of the buffer; and
 *      * The section type must be 1 through 7, inclusive.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] sec              Pointer to decoded section.
 * @param[in]  buf              Pointer to start of encoded section.
 * @param[in]  bufLen           Length of buffer in bytes.
 * @retval     0                Success. \c *secLen and \c *secType are set.
 * @retval     G2D_INVALID      Invalid section.
 */
static int g2s_decodeLengthAndType(
    Grib2Section* const         sec,
    const unsigned char* const  buf,
    const size_t                bufLen)
{
    g2int                       secLen;
    g2int                       secType;

    if (bufLen < 5) {
        log_add("Remaining GRIB-2 message too short to contain valid "
                "section: %lu bytes remaining", (unsigned long)bufLen);
        return G2D_INVALID;
    }

    gbit(buf, &secLen, 0, 4*CHAR_BIT);

    if (5 > secLen || bufLen < secLen) {
        log_add("Invalid section-length parameter: value=%ld; %lu bytes "
                "remaining", (long)secLen, (unsigned long)bufLen);
        return G2D_INVALID;
    }

    gbit(buf+4, &secType, 0, 1*CHAR_BIT);

    if (!g2s_isValid(secType)) {
        log_add("Invalid section-type parameter: %ld", (long)secType);
        return G2D_INVALID;
    }

    sec->buf = buf;
    sec->len = secLen;
    sec->type = secType;

    return 0;
}

/**
 * Decodes and validates the start of a section. For a section to be valid, the
 * following must be true:
 *
 *      * The length parameter of the section must be less than or equal to the
 *        length of the buffer; and
 *      * The section type must be 1 through 7, inclusive.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] sec              Pointer to decoded section.
 * @param[in]  buf              Pointer to start of encoded section.
 * @param[in]  bufLen           Length of buffer in bytes.
 * @retval     0                Success. \c *sec is partially set.
 * @retval     G2D_INVALID      Invalid section.
 * @retval     G2D_END          Last section (i.e., "7777" sentinel
 *                              encountered).
 */
static int g2s_decodeStart(
    Grib2Section* const         sec,
    const unsigned char* const  buf,
    const size_t                bufLen)
{
    int                         status;
    g2int                       secLen;
    g2int                       secType;

    if ((status = g2s_isLastSection(buf, bufLen)))
        return status;

    if ((status = g2s_decodeLengthAndType(sec, buf, bufLen)))
        return status;

    return 0;
}

/**
 * Initializes a decoded section from an encoded section.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] sec              Pointer to decoded section.
 * @param[in]  buf              Pointer to start of encoded section.
 * @param[in]  bufLen           Length of buffer in bytes.
 * @retval     0                Success. \c *sec is set.
 * @retval     G2D_END          Last section (i.e., "7777" sentinel
 *                              encountered).
 * @retval     G2D_INVALID      Invalid section.
 * @retval     G2D_SYSERR       System failure.
 */
static int g2s_init(
    Grib2Section* const        sec,
    const unsigned char* const buf,
    const size_t               bufLen)
{
    int          status;
    size_t       secLen;
    g2int        secType;

    if ((status = g2s_decodeStart(sec, buf, bufLen)))
        return status;

    if ((status = g2s_validate(sec)))
        return status;

    return 0;
}

/**
 * Returns the type of a section.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in] sec  Pointer to the section.
 * @return         The type of the section.
 */
static unsigned g2s_getType(
    const Grib2Section* const sec)
{
    return sec->type;
}

/**
 * Returns the length of the encoded section associated with a decoded section
 * in bytes.
 *
 * @param[in] sec  Pointer to the decoded section.
 * @return         The length of the associated encoded section in bytes.
 */
static size_t g2s_getLength(
    const Grib2Section* const sec)
{
    return sec->len;
}

int g2s_getG2Int(
    const Grib2Section* const section,
    const size_t              iByte,
    const unsigned            nBytes,
    g2int* const              value)
{
    if (iByte + nBytes > section->len) {
        log_add("Invalid byte-spec: iByte=%lu, nBytes=%u, bufLen=%lu", iByte,
                nBytes, section->len);
        return G2D_INVALID;
    }

    gbit(section->buf+iByte, value, 0, nBytes*CHAR_BIT);

    return 0;
}

/**
 * Initializes a list of decoded sections from the relevant portion of an
 * encoded GRIB-2 message. The first section in the list will be section 1.
 *
 * Not atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] list              Pointer to the list of sections.
 * @param[in]     buf               Pointer to the start of encoded section 1.
 * @param[in]     len               The length of the buffer in bytes.
 * @retval        0                 Success.
 * @retval        G2D_INVALID       Invalid message. Possibly no section 1.
 * @retval        G2D_SYSERR        System error.
 */
static int sl_init(
    List* const    list,
    unsigned char* buf,
    size_t         len)
{
    int status;
    int isFirst = 1;

    for (;;) {
        Grib2Section sec;
        size_t       secLen;

        if ((status = g2s_init(&sec, buf, len))) {
            if (G2D_END == status)
                status = 0;
            break;
        }

        if (isFirst && g2s_getType(&sec) != 1) {
            status = G2D_INVALID;
            break;
        }
        isFirst = 0;

        if ((status = list_append(list, &sec)))
            break;

        secLen = g2s_getLength(&sec);
        buf += secLen;
        len -= secLen;
    }

    return status;
}

/**
 * Clears a list of decoded sections.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] list  Pointer to list of decoded sections.
 */
static void sl_clear(
    List* const list)
{
    list_clear(list);
}

/**
 * Initializes a decoded GRIB-2 field from associated decoded GRIB-2 sections.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] field       Address of pointer to GRIB-2 field.
 * @param[in]  sections    Pointer to associated sections.
 * @param[in]  decoded     Pointer to enclosing decoded GRIB-2 message.
 * @retval     0           Success. \c *field is set.
 * @retval     G2D_INVALID A section is missing.
 */
static int g2f_init(
    Grib2Field* const      field,
    FieldSections          sections,
    DecodedGrib2Msg* const decoded)
{
    int i;

    for (i = 0; i < G2F_NUM_SEC; i++) {
        if (NULL == sections[i]) {
            log_add("Missing section of type %d", i);
            return G2D_INVALID;
        }
    }

    for (i = 0; i < G2F_NUM_SEC; i++)
        field->sections[i] = sections[i];

    field->decoded = decoded;

    return 0;
}

/**
 * Returns the GRIB-2 section of a GRIB-2 field corresponding to a given index.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  field        Pointer to decoded GRIB-2 field.
 * @param[in]  index        GRIB-2 index of the field's section to return (1,
 *                          3-7).
 * @param[out] section      Address of pointer to section.
 * @retval     0            Success. \c *section is set.
 * @retval     G2D_INVALID  Invalid index.
 */
int g2f_getSection(
    const Grib2Field* const   field,
    const unsigned            index,
    Grib2Section** const      section)
{
    if (index == 1) {
        *section = field->decoded->sec1;
    }
    else {
        if (index < G2F_MIN_SEC || index > G2F_MAX_SEC) {
            log_add("Invalid section index: %u", index);
            return G2D_INVALID;
        }

        *section = field->sections[index - G2F_MIN_SEC];
    }

    return 0;
}

/**
 * Initializes the sections of a decoded GRIB-2 message from an encoded GRIB-2
 * message.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] decoded           Pointer to the decoded GRIB-2 message.
 * @param[in]  buf               Pointer to the start of encoded section 1.
 * @param[in]  bufLen            The length of the buffer in bytes.
 * @retval     0                 Success.
 * @retval     G2D_INVALID       Invalid message.
 * @retval     G2D_SYSERR        System error.
 */
static int g2d_initSections(
    DecodedGrib2Msg* const decoded,
    unsigned char*         buf,
    size_t                 bufLen)
{
    /*
     * Set the initial size of the list of sections sufficient to contain
     * section 1 plus one field (section types 3 through 7, inclusive).
     */
    int    status = list_init(&decoded->sections, sizeof(Grib2Section), 6);

    if (status)
        return status;

    if ((status = sl_init(&decoded->sections, buf, bufLen))) {
        sl_clear(&decoded->sections);
        return status;
    }

    decoded->sec1 = (Grib2Section*)list_getElement(&decoded->sections, 0);

    return 0;
}

/**
 * Clears the list of sections of a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded  Pointer to the decoded GRIB-2 message.
 */
static void g2d_clearSections(
    DecodedGrib2Msg* const decoded)
{
    sl_clear(&decoded->sections);
}

/**
 * Initializes the list of decoded fields in a decoded GRIB-2 message.
 *
 * Not atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded     Pointer to decoded GRIB-2 message.
 * @retval        0           Success. Field list is set.
 * @retval        G2D_INVALID A section is missing.
 * @retval        G2D_SYSERR  System error.
 */
static int g2d_initFieldList(
    DecodedGrib2Msg* const decoded)
{
    List* const   fields = &decoded->fields;
    List* const   sections = &decoded->sections;
    unsigned      iSec;
    unsigned      numSecs = list_getSize(sections);
    FieldSections secs;

    /*
     * Start at index 1 because the first section is type 1 and may be safely
     * ignored.
     */
    for (iSec = 1; iSec < numSecs; iSec++) {
        Grib2Section* const sec =
                (Grib2Section*)list_getElement(sections, iSec);
        const unsigned secType = g2s_getType(sec);
        const unsigned iElt = secType - G2F_MIN_SEC;

        secs[iElt] = sec;

        if (secType == G2F_MAX_SEC) {
            int         status;
            Grib2Field  field;

            if ((status = g2f_init(&field, secs, decoded)))
                return status;

            if ((status = list_append(fields, &field)))
                return status;
        }
    }

    return 0;
}

/**
 * Clears a list of fields.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] fields  Pointer to the list of fields.
 */
static void g2d_clearFieldList(
    List* const fields)
{
    list_clear(fields);
}

/**
 * Initializes the fields of a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded     Pointer to decoded GRIB-2 message.
 * @retval        0           Success. \c *decoded contains a list of the
 *                            fields.
 * @retval        G2D_INVALID A section is missing.
 * @retval        G2D_SYSERR  System error.
 */
static int g2d_initFields(
    DecodedGrib2Msg* const decoded)
{
    int       status;
    unsigned  numSecs = list_getSize(&decoded->sections);

    /*
     * At least one field.
     */
    if ((status = list_init(&decoded->fields, sizeof(Grib2Field), 1))) {
        log_add("Couldn't initialize list of fields");
        return status;
    }

    if ((status = g2d_initFieldList(decoded))) {
        g2d_clearFieldList(&decoded->fields);
        return status;
    }

    return 0;
}

/**
 * Clears the fields of a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded  Pointer to the decoded GRIB-2 message.
 */
static void g2d_clearFields(
    DecodedGrib2Msg* const decoded)
{
    g2d_clearFieldList(&decoded->fields);
}

/**
 * Initializes a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in] decoded          Pointer to the decoded GRIB-2 message.
 * @param[in] buf              Pointer to the start of an encoded GRIB-2
 *                             message. The message must start with the
 *                             character sequence "GRIB". The client must not
 *                             alter or free the message until use of this
 *                             module on it is no longer needed.
 * @param[in] bufLen           The length of the message in bytes.
 * @retval    0                Success. \c *decoded is initialized.
 * @retval    G2D_INVALID      Invalid message.
 * @retval    G2D_NOT_2        Not GRIB edition 2.
 * @retval    G2D_SYSERR       System error.
 */
static int g2d_init(
    DecodedGrib2Msg* const decoded,
    unsigned char*         buf,
    size_t                 bufLen)
{
    int    status;
    size_t secLen;

    if ((status = sec0_init(&decoded->sec0, &secLen, buf, bufLen))) {
        log_add("Couldn't decode section 0 of GRIB-2 message");
        return status;
    }

    decoded->buf = buf;
    decoded->bufLen = bufLen;

    buf += secLen;
    bufLen -= secLen;

    if ((status = g2d_initSections(decoded, buf, bufLen))) {
        log_add("Couldn't decode sections of GRIB-2 message after section 0");
        sec0_clear(&decoded->sec0);
        return status;
    }

    if ((status = g2d_initFields(decoded))) {
        log_add("Couldn't create fields of GRIB-2 message");
        g2d_clearSections(decoded);
        sec0_clear(&decoded->sec0);
        return status;
    }

    return 0;
}

/**
 * Clears a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded  Pointer to decoded GRIB-2 message.
 */
static void g2d_clear(
    DecodedGrib2Msg* const decoded)
{
    g2d_clearFields(decoded);
    g2d_clearSections(decoded);
}

/**
 * Returns a decoded GRIB-2 message corresponding to an encoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[out] decoded          Address of pointer to decoded GRIB-2 message.
 *                              The client should call \c g2d_free(*decoded)
 *                              when the decoded GRIB-2 message is no longer
 *                              needed.
 * @param[in]  buf              Pointer to the start of an encoded GRIB-2
 *                              message. The message must start with the
 *                              character sequence "GRIB". The client must not
 *                              alter or free the message until the client calls
 *                              \c g2d_free().
 * @param[in]  bufLen           The length of the message in bytes.
 * @retval     0                Success. \c *decoded is set.
 * @retval     G2D_INVALID      Invalid message.
 * @retval     G2D_NOT_2        Not GRIB edition 2.
 * @retval     G2D_SYSERR       System error.
 */
int g2d_new(
    DecodedGrib2Msg** const decoded,
    const unsigned char*    buf,
    size_t                  bufLen)
{
    int                    status;
    DecodedGrib2Msg* const msg = (DecodedGrib2Msg*)log_malloc(
            sizeof(DecodedGrib2Msg), "decoded GRIB-2 message");

    if (!msg)
        return G2D_SYSERR;

    if ((status = g2d_init(msg, buf, bufLen))) {
        free(msg);
        return status;
    }

    *decoded = msg;

    return 0;
}

/**
 * Frees a decoded GRIB-2 message.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] decoded  Pointer to decoded GRIB-2 message or NULL.
 */
void g2d_free(
    DecodedGrib2Msg* const decoded)
{
    if (decoded) {
        g2d_clear(decoded);
        free(decoded);
    }
}

/**
 * Returns the associated encoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Pointer to encoded GRIB-2 message.
 */
const unsigned char* g2d_getBuf(
    DecodedGrib2Msg* decoded)
{
    return decoded->buf;
}

/**
 * Returns the length, in bytes, of the associated encoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Length of the associated encoded GRIB-2 message in bytes.
 */
size_t g2d_getBufLen(
    DecodedGrib2Msg* decoded)
{
    return decoded->bufLen;
}

/**
 * Returns the section 1 of a decoded GRIB-2 message.
 *
 * @param[in] decoded  Pointer to decoded GRIB-2 message.
 * @return             Pointer to section 1 of the decoded GRIB-2 message.
 */
const Grib2Section* g2d_getSection1(
    DecodedGrib2Msg* decoded)
{
    return decoded->sec1;
}

/**
 * Returns the originating center.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded            Pointer to decoded GRIB-2 message.
 * @return                        The ID of the originating center.
 */
g2int g2d_getOriginatingCenter(
    DecodedGrib2Msg* const decoded)
{
    g2int value;

    gbit(decoded->sec1->buf+5, &value, 0, 2*CHAR_BIT);

    return value;
}

/**
 * Returns the originating sub-center.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded            Pointer to decoded GRIB-2 message.
 * @return                        The ID of the originating sub-center.
 */
g2int g2d_getOriginatingSubCenter(
    DecodedGrib2Msg* const decoded)
{
    g2int value;

    gbit(decoded->sec1->buf+7, &value, 0, 2*CHAR_BIT);

    return value;
}

/**
 * Returns the number of fields in a decoded GRIB-2 message.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in] decoded  Pointer to the decoded GRIB-2 message
 * @return             The number of fields in the decoded GRIB-2 message.
 */
size_t g2d_getNumFields(
    const DecodedGrib2Msg* const decoded)
{
    return list_getSize(&decoded->fields);
}

/**
 * Returns the GRIB-2 field corresponding to a given index.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded      Pointer to the decoded GRIB-2 message.
 * @param[in]  index        0-based index of the field to return.
 * @param[out] field        Address of pointer to field.
 * @retval     0            Success. \c *field is set.
 * @retval     G2D_INVALID  Invalid index.
 */
int g2d_getField(
    const DecodedGrib2Msg* const decoded,
    const unsigned               index,
    const Grib2Field** const     field)
{
    const size_t numFields = list_getSize(&decoded->fields);

    if (index >= numFields) {
        log_add("Invalid field index: index=%u, numFields=%lu", index,
                (unsigned long)numFields);
        return G2D_INVALID;
    }

    *field = (Grib2Field*)list_getElement(&decoded->fields, index);

    return 0;
}

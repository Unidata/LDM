/*
 *   Copyright 2014, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include "config.h"
#include "gempak/gb2def.h"
#include "log.h"
#include "StringBuf.h"

#define USE_GRIB2_DECODER 0
#if USE_GRIB2_DECODER
#include "Grib2Decoder.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * The following are declared here because they're not declared elsewhere.
 */
extern const char*      s_pds_center(unsigned char center, unsigned char subcenter);
extern const char*      s_pds_model(unsigned char center, unsigned char model);
extern int              decode_g2gnum(gribfield *gfld);
extern int              wmo_to_gridid (const char *TT, const char *AA );

#if USE_GRIB2_DECODER

#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef struct {
    unsigned byteOff;  /**< Byte-offset to start of parameter */
    unsigned numBytes; /**< Number of bytes in parameter */
} ParamInfo;

/**
 * Decodes encoded parameters in a decoded section into a \c g2int array.
 *
 * Not atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  sec        Pointer to decoded section.
 * @param[in]  paramInfo  Location and length of parameters.
 * @param[in]  numParams  Number of parameters.
 * @param[out] params     Pointer to first element of parameter array.
 * @retval     0          Success. \c params[0] through \c params[numParams-1]
 *                        are set.
 * @retval     1          Not enough parameters in decoded section.
 */
static int getG2intParams(
    Grib2Section* const    sec,
    const ParamInfo* const paramInfo,
    const unsigned         numParams,
    g2int* const           params)
{
    int i;

    for (i = 0; i < numParams; i++) {
        int status = g2s_getG2Int(sec, paramInfo[i].byteOff,
                paramInfo[i].numBytes, params+i);

        if (status)
            return 1; /* Corrupt message */
    }

    return 0;
}

/**
 * Creates a new section 1 in a \c gribfield structure from a decoded GRIB-2
 * field.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] gfld   Pointer to the \c gribfield structure. Client should call
 *                    \c sec1_free(gfld) when the section 1 data is no longer
 *                    needed.
 * @param[in]  field  Pointer to the decoded GRIB-2 field.
 * @retval     0      Success. \c gfld->idsect and \c gfld->idsectlen are set.
 * @retval     1      Corrupt GRIB-2 message.
 * @retval     3      System error.
 */
static int sec1_new(
    gribfield* const  gfld,
    Grib2Field* const field)
{
    /**
     * Location and length of encoded section 1 parameters.
     */
    static ParamInfo    paramInfo[] = {
            { 5, 2}, { 7, 2}, { 9, 1}, {10, 1}, {11, 1}, {12, 2}, {14, 1},
            {15, 1}, {16, 1}, {17, 1}, {18, 1}, {19, 1}, {20, 1}
    };
    static unsigned     numParams = sizeof(paramInfo)/sizeof(paramInfo[0]);
    Grib2Section*       sec1;
    g2int*              params = (g2int*)LOG_MALLOC(numParams*sizeof(g2int),
            "section 1 parameter array");
    int                 status;

    if (NULL == params)
        return 3; /* System error */

    (void)g2f_getSection(field, 1, &sec1);

    if (status = getG2intParams(sec1, paramInfo, numParams, params)) {
        free(params);
        return status;
    }

    gfld->idsect = params;
    gfld->idsectlen = numParams;

    return 0;
}

/**
 * Frees a section 1 in a \c gribfield structure.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld  Pointer to the \c gribfield structure.
 */
static void sec1_free(
    gribfield* const gfld)
{
    free(gfld->idsect);
}

/**
 * Creates section 3 information in a \c gribfield structure from a decoded
 * GRIB-2 field.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld   Pointer to \c gribfield structure. Client should call
 *                       \c sec3_free(gfld) when the information is no longer
 *                       needed.
 * @param[in]     field  Pointer to decoded GRIB-2 field.
 * @retval        0      Success. \c gfld is set.
 */
static int sec3_new(
    gribfield* const  gfld,  /**< Pointer to \c gribfield structure */
    Grib2Field* const field) /**< Pointer to decoded GRIB-2 field */
{
    /**
     * Location and length of encoded section 3 parameters.
     */
    static ParamInfo    paramInfo[] = {
            { 5, 1}, /**< Source of grid definition */
            { 6, 4}, /**< Number of grid points */
            {10, 1}, /**< Number of bytes for optional list */
            {11, 1}, /**< Interpretation of optional list */
            {12, 2}, /**< Grid definition template number */
    };
    static unsigned     numParams = sizeof(paramInfo)/sizeof(paramInfo[0]);
    g2int               params[numParams];
    Grib2Section        sec3;
    int                 status;

    (void)g2f_getSection(field, 1, &sec3);

    if (status = getG2intParams(sec3, paramInfo, numParams, params))
        return status;

    /*
    int jerr = g2_unpack3(cgrib, sz, &iofst, &igds, &lgfld->igdtmpl,
                  &lgfld->igdtlen, &lgfld->list_opt, &lgfld->num_opt);
     */

    gfld->griddef = params[0];
    gfld->ngrdpts = params[1];
    gfld->numoct_opt = params[2];
    gfld->interp_opt = params[3];
    gfld->igdtnum = params[4];

    // g2_getfld();

    return 0;
}

/**
 * Frees the section 3 information in a \c gribfield structure.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld  Pointer to \c gribfield structure.
 */
static void sec3_free(
    gribfield const gfld)
{
}

/**
 * Creates section 4 information in a \c gribfield structure from a decoded
 * GRIB-2 field.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld   Pointer to \c gribfield structure. Client should call
 *                       \c sec4_free(gfld) when the information is no longer
 *                       needed.
 * @param[in]     field  Pointer to decoded GRIB-2 field.
 * @retval        0      Success. \c gfld is set.
 */
static int sec4_new(
    gribfield* const  gfld,  /**< Pointer to \c gribfield structure */
    Grib2Field* const field) /**< Pointer to decoded GRIB-2 field */
{
    /**
     * Location and length of encoded section 4 parameters.
     */
    static ParamInfo    paramInfo[] = {
            { 5, 1}, /**< Source of grid definition */
            { 6, 4}, /**< Number of grid points */
            {10, 1}, /**< Number of bytes for optional list */
            {11, 1}, /**< Interpretation of optional list */
            {12, 2}, /**< Grid definition template number */
    };
    static unsigned     numParams = sizeof(paramInfo)/sizeof(paramInfo[0]);
    g2int               params[numParams];
    Grib2Section        sec3;
    int                 status;

    (void)g2f_getSection(field, 1, &sec3);

    if (status = getG2intParams(sec3, paramInfo, numParams, params))
        return status;

    /*
    int jerr = g2_unpack3(cgrib, sz, &iofst, &igds, &lgfld->igdtmpl,
                  &lgfld->igdtlen, &lgfld->list_opt, &lgfld->num_opt);
     */

    gfld->griddef = params[0];
    gfld->ngrdpts = params[1];
    gfld->numoct_opt = params[2];
    gfld->interp_opt = params[3];
    gfld->igdtnum = params[4];

    // g2_getfld();

    return 0;
}


/**
 * Initializes a \c gribfield structure from a decoded GRIB-2 field.
 *
 * Not atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] gfld       Pointer to \c gribfield structure. Client should call
 *                        \c gfld_clear(gfld) when the structure is no longer
 *                        needed.
 * @param[in]  field      Pointer to field in a GRIB message.
 * @param[in]  index      0-based index of the field in the GRIB message.
 * @retval     0          Success. \c *gfld is set.
 * @retval     1          Corrupt GRIB-2 message.
 * @retval     3          System error.
 */
static int gfld_init(
    gribfield* const  gfld,
    Grib2Field* const field,
    const unsigned    index)
{
    int status;

    (void)memset(gfld, 0, sizeof(*gfld));

    if (status = sec1_new(gfld, field))
        return status;

    if (status = sec3_new(gfld, field)) {
        sec1_free(gfld);
        return status;
    }

    if (status = sec4_new(gfld, field)) {
        sec3_free(gfld);
        sec1_free(gfld);
        return status;
    }
    // g2_getfld();

    return 0;
}

/**
 * Clears a \c gribfield structure.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld  Pointer to \c gribfield structure.
 */
static void gfld_clear(
    gribfield* const gfld)
{
    sec1_free(gfld);
    sec3_free(gfld);
    sec4_free(gfld);
}

/**
 * Returns a new \c gribfield structure initialized from a field in a GRIB
 * message.
 *
 * @param[out] gribField  Address of pointer to \c gribfield structure. Client
 *                        should call \c gfld_free(*gribField) when the
 *                        structure is no longer needed.
 * @param[in]  field      Pointer to field in a GRIB message.
 * @param[in]  index      0-based index of the field in the GRIB message.
 * @retval     0          Success. \c *gribField is set.
 * @retval     1          System error.
 */
static int gfld_new(
    gribfield** const gribField,
    Grib2Field* const field,
    const unsigned    index)
{
    static char      g2tables[5][LLMXLN];
    static char*     tbllist[5] = {g2tables[0], g2tables[1], g2tables[2],
            g2tables[3], g2tables[4]};
    int              status;
    const gribfield* gfld = (gribfield*)LOG_MALLOC(sizeof(gribfield),
            "GRIB field");

    if (NULL == gfld) {
        status = 1;
    }
    else {
        if (status = gfld_init(gfld, field, index))
            free(gfld);
        else
            *gribField = gfld;
    }

    return status;
}

/**
 * Frees a \c gribfield structure.
 *
 * Atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] gfld  Pointer to \c gribfield structure.
 */
static void gfld_free(
    gribfield* const gfld)
{
    free(gfld);
}

/**
 * Initializes a \c Gribmsg structure from a decoded GRIB message. \c
 * gfld_init() must still be called.
 *
 * @param[out] gribMsg    Pointer to the \c Gribmsg structure to initialize.
 * @param[in]  decoded    Pointer to the decoded GRIB message.
 */
static void gmsg_init(
    Gribmsg* const         gribMsg,
    DecodedGrib2Msg* const decoded)
{
    gribMsg->cgrib2 = g2d_getBuf(decoded);
    gribMsg->mlength = g2d_getBufLen(decoded);
    gribMsg->gfld = NULL;
    gribMsg->field_tot = g2d_getNumFields(decoded);
}

static void gemInfo_init(
    Geminfo* const gemInfo,
    Gribmsg* const gribMsg)
{
    gb2_2gem(&curr_g2, &curr_gem, tbllist, &ier);

    if (ier != 0) {
       sprintf(g2name,"UNK\0");
       sprintf(levelstmp,"LVL\0");
       sprintf(fdats,"FHRS\0");
    }
    else {
       sprintf(g2name,"%s\0",curr_gem.parm);
       cst_rmbl (g2name, g2name, &ilen, &ier );
       if ( n > 0 ) strncat ( prods, ";", 1);
       sprintf(prods+strlen(prods),"%s\0",g2name);

       strptr[0] = (char *)malloc(12);
       cst_itoc ( &curr_gem.vcord, 1, (char **)(&strptr), &ier);

       cst_rxbl (curr_gem.unit, curr_gem.unit, &ilen, &ier);
       if ( ilen == 0 ) sprintf (curr_gem.unit, "-\0");
       if ( curr_gem.level[1] == -1 )
          sprintf(levelstmp,"%d %s %s\0",curr_gem.level[0],curr_gem.unit,strptr[0]);
       else
          sprintf(levelstmp,"%d-%d %s %s\0",curr_gem.level[0],curr_gem.level[1],curr_gem.unit,strptr[0]);

       cst_rmbl (curr_gem.gdattm1, curr_gem.gdattm1, &ilen, &ier );
       cst_rmbl (curr_gem.gdattm2, curr_gem.gdattm2, &ilen, &ier );
       if ( ilen > 0 )
          sprintf(fdats,"%s-%s\0",curr_gem.gdattm1,curr_gem.gdattm2);
       else
          sprintf(fdats,"%s\0",curr_gem.gdattm1);

       ilen = 1;
       while ( ilen > 0 ) cst_rmst(fdats, "/", &ilen, fdats, &ier);

       free(strptr[0]);
    }
}

/**
 * Appends the name of the parameter in a GEMPAK field to a string-buffer.
 *
 * Not atomic,
 * Not idempotent,
 * Thread-safe
 *
 * @param[in,out] buf       Pointer to string-buffer.
 * @param[in]     gemInfo   Pointer to GEMPAK information on the parameter.
 * @retval        0         Success. \c buf is set.
 * @retval        3         System error.
 */
static int appendParameterName(
    StringBuf* const     buf,
    const Geminfo* const gemInfo)
{
    char   g2name[13];
    int    nameLen;
    int    status;

    strncpy(g2name, gemInfo->parm, sizeof(g2name))[sizeof(g2name)-1] = 0;
    cst_rmbl(g2name, g2name, &nameLen, &status);

    if (strBuf_getLength(buf)) {
        if (strBuf_appendString(buf, ";")) {
            log_add("Couldn't append parameter-name separator");
            return 3;
        }
    }

    if (strBuf_appendString(buf, g2name)) {
        log_add("Couldn't append parameter-name \"%s\"", g2name);
        return 3;
    }

    return 0;
}

/**
 * Appends the names of field parameters of a decoded GRIB-2 message to a
 * string-buffer. Returns the \c Gribmsg and \c Geminfo structures for the last
 * field in the message.
 *
 * @param[in,out] buf       Pointer to string-buffer to hold the string of
 *                          parameter names.
 * @param[in]     decoded   Pointer to decoded GRIB-2 message.
 * @param[out]    gribMsg   Pointer to \c Gribmsg structure.
 * @param[out]    gemField  Pointer to \c Geminfo structure.
 * @param[out]    modelId   Pointer to model ID of last field in message.
 * @param[out]    gridId    Pointer to grid ID of last field in message.
 * @retval        0         Success. \c *buf is set. \c *gribMsg, \c
 *                          *gemField, and \c *modelId are set based on the last
 *                          field of the message.
 * @retval        1         GRIB-2 message has no fields.
 * @retval        1         Corrupt GRIB-2 message.
 * @retval        3         System error.
 */
static int appendParameterNames(
    StringBuf* const buf,
    DecodedGrib2Msg* const decoded,
    Gribmsg* const   gribMsg,
    Geminfo* const   gemInfo,
    int* const       modelId,
    int* const       gridId)
{
    unsigned         i;
    const size_t     numFields = g2d_getNumFields(decoded);

    if (0 == numFields)
        return 1;

    gmsg_init(&gribMsg, decoded);

    for (i = 0; i < numFields; i++) {
        Grib2Field*   field;
        gribfield     gfld;
        int           status;

        (void)g2d_getField(decoded, i, &field);

        if (status = gfld_init(&gfld, field, i)) {
            log_add("Couldn't initialize gribfield structure");
        }
        else {
            gribMsg->gfld = gfld;

            gemInfo_init(gemInfo, gribMsg);

            status = appendParameterName(buf, &gemInfo);

            if (0 == status && i == numFields - 1) {
                *modelId = gfld->ipdtmpl[4];
                *gridId = (gfld->griddef == 0)
                    ? decode_g2gnum(gfld)
                    : gfld->griddef;
            }

            gfld_clear(gfld);
        } /* "gfld" initialized */

        if (status)
            return status;
    }

    return 0;
}

/**
 * Gets the model ID from the last field in a decoded GRIB-2 message.
 *
 * Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[in]  decoded  Pointer to decoded GRIB-2 message.
 * @param[out] modelId  Pointer to model ID of last field in message.
 * @retval     0        Success. \c *modelId is set.
 * @retval     1        No fields in decoded GRIB-2 message.
 */
static int getModelId(
    DecodedGrib2Msg* const decoded,
    int* const             modelId)
{
    Grib2Field*   field;
    Grib2Section* section;
    const size_t  numFields = g2d_getNumFields(decoded);
    g2int         id;

    if (0 == numFields)
        return 1;

    (void)g2d_getField(decoded, numFields-1, &field); /* field must exist */
    (void)g2f_getSection(field, 4, &section); /* section must exist */

    if (g2s_getG2Int(section, 13, 1, &id)) {
        log_add("Couldn't get model ID");
        return 1;
    }

    *modelId = id;

    return 0;
}

static void composeIdent(
    char* const            ident,
    size_t const           identSize,
    DecodedGrib2Msg* const decoded,
    Gribmsg* const         gribMsg,
    Geminfo* const         gemField,
    StringBuf* const       prods,
    const char* const      wmoHead,
    int const              modelId)
{
    g2int originatingCenter = g2d_getOriginatingCenter(decoded);

    snprintf(ident, identSize, "grib2/%s/%s/#%03d/%s/%s/%s\0",
            s_pds_center(originatingCenter,
                    g2d_getOriginatingSubCenter(decoded)),
            s_pds_model(originatingCenter, modelId),
            grid_id,
            fdats,
            prods,
            levelstmp);
}

/**
 * Sets an LDM product-identifier from a GRIB message.
 *
 * Not Atomic,
 * Idempotent,
 * Thread-safe
 *
 * @param[out] ident      Product-identifier buffer.
 * @param[in]  identSize  Size of product-identifier buffer in bytes.
 * @param[in]  decoded    Decoded GRIB message.
 * @param[in]  wmoHead    WMO header associated with the GRIB message.
 * @retval     0          Success. \c *ident is set.
 * @retval     1          GRIB message has no data.
 * @retval     3          System error.
 */
static int setIdent(
    char* const            ident,
    const size_t           identSize,
    DecodedGrib2Msg* const decoded,
    const char* const      wmoHead)
{
    const size_t   numFields = g2d_getNumFields(decoded);
    int            status;

    if (0 == numFields) {
        log_add("GRIB message has no fields");
        status = 1;
    }
    else {
        StringBuf* const prods = strBuf_new(127); /* initially empty */

        if (NULL == prods) {
            log_add_syserr("Couldn't allocate string-buffer for products");
            log_flush_error();
            status = 3;
        }
        else {
            Gribmsg          gribMsg;
            Geminfo          gemInfo;
            int              modelId;

            status = appendParameterNames(prods, decoded, &gribMsg, &gemInfo);

            if (0 == status) {
                status = getModelId(decoded, &modelId);

                if (0 == status)
                    composeIdent(ident, identSize, decoded, &gribMsg, &gemInfo,
                            prods, wmoHead, modelId);
            }

            strBuf_free(prods);
        } /* "prods" allocated */
    } /* numFields > 0 */

    return status;
}

#endif /* USE_GRIB2_DECODED */

/**
 * Generates an LDM product-identifier from a GRIB edition 2 message.
 *
 * Atomic,
 * Idempotent,
 * Not thread-safe
 *
 * @param[in]  data             Pointer to the GRIB message.
 * @param[in]  sz               Length of the GRIB message in bytes.
 * @param[in]  wmohead          Pointer to the associated WMO header string.
 * @param[out] ident            Pointer to a buffer to receive the LDM
 *                              product-identifier.
 * @param[in]  identSize        Size of the \c ident buffer in bytes.
 * @retval     0                Success. \c ident is set and NUL-terminated.
 * @retval     1                Invalid GRIB message.
 * @retval     2                GRIB message isn't edition 2.
 * @retval     3                System error.
 */
int grib2name (
    char* const         data,
    const size_t        sz,
    const char* const   wmohead,
    char* const         ident,
    const size_t        identSize)
{
#if USE_GRIB2_DECODER
    int                 status;
    DecodedGrib2Msg*    decoded;

    if (status = g2d_new(&decoded, (unsigned char*)data, sz)) {
        log_add("Couldn't decode GRIB message");
        status = G2D_INVALID == status
                ? 1
                : G2D_NOT_2
                    ? 2
                    : 3;
    }
    else {
        if (status = setIdent(ident, identSize, decoded, wmohead)) {
            log_add("Couldn't set LDM product-identifier");
            status = 1;
        }

        g2d_free(decoded);
    } /* "decoded" allocated */

    return status;
#else
    static StringBuf*  paramNames;    /* Buffer for parameter name(s) */
    int                iField;        /* GRIB-2 field index */
    int                status;        /* Function return code */
    g2int              listsec0[3];   /* GRIB-2 section 0 parameters */
    g2int              listsec1[13];  /* GRIB-2 section 1 parameters */
    g2int              numlocal;      /* Number of GRIB section 2-s */
    int                model_id = 0;  /* ID of model */
    int                grid_id;       /* ID of grid */
    char               fdats[80];     /* No idea */
    char               levelstmp[80]; /* Level? */
    Gribmsg            g2Msg;         /* GRIB-2 message structure */

    if (paramNames) {
        strBuf_clear(paramNames);
    }
    else {
        paramNames = strBuf_new(127);
        if (NULL == paramNames) {
            log_add("Couldn't allocate buffer for parameter name(s)");
            return 3;
        }
    }

    g2Msg.cgrib2 = (unsigned char*)data;
    g2Msg.mlength = sz;
    g2Msg.gfld = NULL;
    g2Msg.field_tot = 0;

    if ((status = g2_info(g2Msg.cgrib2, g2Msg.mlength, listsec0, listsec1,
            &(g2Msg.field_tot), &numlocal)) != 0)
        return (2 == status) ? 2 : 1;

    if (g2Msg.field_tot <= 0) {
        log_add("GRIB-2 message has no data fields");
        return 1;
    }

    for (iField = 0; iField < g2Msg.field_tot; iField++) {
        static char  g2tables[5][LLMXLN];  /* GRIB tables */
        static char* tbllist[5] = {g2tables[0], g2tables[1], g2tables[2],
                g2tables[3], g2tables[4]}; /* Addresses of GRIB tables */
        Geminfo      gemInfo;              /* GEMPAK structure */
        int const    lastField = iField == g2Msg.field_tot - 1;

        status = g2_getfld(g2Msg.cgrib2, g2Msg.mlength, iField+1, 0, 0,
                &g2Msg.gfld);

        if (status) {
            log_add("Invalid GRIB-2 message: g2_getfld() status=%d", status);
            return (2 == status) ? 2 : 1;
        }

        /* "g2Msg.gfld" is allocated */

        /* Initialize strings in Geminfo structure */
        (void)memset(gemInfo.cproj, 0, sizeof(gemInfo.cproj));
        (void)memset(gemInfo.parm, 0, sizeof(gemInfo.parm));
        (void)memset(gemInfo.gdattm1, 0, sizeof(gemInfo.gdattm1));
        (void)memset(gemInfo.gdattm2, 0, sizeof(gemInfo.gdattm2));

        /*
         * In the original code, the last field determined the model ID.
         */
        if (lastField)
            model_id = g2Msg.gfld->ipdtmpl[4];

        /*
         * This assignment to "grid_id" isn't under the above "lastField"
         * conditional because "decode_g2gnum()" might have side-effects upon
         * which "gb2_2gem()" depends.
         */
        grid_id = (g2Msg.gfld->griddef == 0)
            ? decode_g2gnum(g2Msg.gfld)
            : g2Msg.gfld->griddef;

        gb2_2gem(&g2Msg, &gemInfo, tbllist, &status);

        if (status) {
            log_add("Couldn't decode GRIB2 message. WMO header=\"%s\"",
                    wmohead);
            log_flush_error();

            if (lastField) {
                (void)strcpy(fdats, "FHRS"); /* safe */
                (void)strcpy(levelstmp, "LVL"); /* safe */
            }
        }
        else {
            char g2name[13];          /**< Name of product/parameter */
            int  ilen;                /**< Length of resulting string */

            (void)strcpy(g2name, gemInfo.parm); /* both 13 bytes */
            cst_rmbl(g2name, g2name, &ilen, &status);

            if (iField)
                strBuf_appendString(paramNames, ";");
            strBuf_appendString(paramNames, g2name);

            cst_rxbl(gemInfo.unit, gemInfo.unit, &ilen, &status);
            if (ilen == 0)
                (void)strcpy(gemInfo.unit, "-"); /* safe */

            cst_rmbl(gemInfo.gdattm1, gemInfo.gdattm1, &ilen, &status);
            cst_rmbl(gemInfo.gdattm2, gemInfo.gdattm2, &ilen, &status);

            /*
             * In the original code, the last field determined the following
             * parameters.
             */
            if (lastField) {
                static char  strBuf[5];       /* Holds 4-char string */
                static char* strptr = strBuf; /* For "cst_itoc()" */

                if (ilen > 0)
                    (void)snprintf(fdats, sizeof(fdats), "%s-%s",
                            gemInfo.gdattm1, gemInfo.gdattm2);
                else
                    (void)snprintf(fdats, sizeof(fdats), "%s",
                            gemInfo.gdattm1);

                for (ilen = 1; ilen > 0;
                        cst_rmst(fdats, "/", &ilen, fdats, &status));

                cst_itoc(&gemInfo.vcord, 1, &strptr, &status);

                if (gemInfo.level[1] == -1)
                    (void)snprintf(levelstmp, sizeof(levelstmp), "%d %s %s",
                            gemInfo.level[0], gemInfo.unit, strptr);
                else
                    (void)snprintf(levelstmp, sizeof(levelstmp), "%d-%d %s %s",
                            gemInfo.level[0], gemInfo.level[1], gemInfo.unit,
                            strptr);
            }
        }

        g2_free(g2Msg.gfld);
        g2Msg.gfld = NULL;
    }

    /*
     * See if the WMO header can be used for grid 0 products
     */
    if ((grid_id == 0) && (strlen(wmohead) > 11) && (wmohead[7] == 'K') &&
            (wmohead[8] == 'W')) {
        int wmoGridId = wmo_to_gridid(&wmohead[0], &wmohead[2]);

        if (wmoGridId > 0)
            grid_id = wmoGridId;
    }

    (void)snprintf(ident, identSize, "grib2/%s/%s/#%03d/%s/%s/%s",
            s_pds_center((int)listsec1[0], (int)listsec1[1]),
            s_pds_model((int)listsec1[0], model_id),
            grid_id,
            fdats,
            strBuf_toString(paramNames),
            levelstmp);

    return 0;
#endif
}

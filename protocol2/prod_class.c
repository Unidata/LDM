/*
 * See top level file COPYRIGHT for copying and redistribution conditions.
 *
 * Functions for handling product-class specifications.
 */

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>  /* must precede <regex.h> for FreeBSD 4.5-RELEASE cc */
#include <regex.h> 
#include <string.h>

#include "ldm.h"        /* prod_class */
#include "ldmprint.h"
#include "prod_class.h"
#include "timestamp.h" 
#include "RegularExpressions.h"

#include "ulog.h"
#include "log.h"

#ifndef ENOERR
#define ENOERR 0
#endif /*!ENOERR */


const prod_spec _spec_all = {ANY, ".*", 0};
const prod_spec _spec_none = {NONE, NULL, 0};

const prod_class_t _clss_all = {
        {0, 0}, /* TS_ZERO */
        {0x7fffffff, 999999}, /* TS_ENDT */
        {
                1,
                (prod_spec *)&_spec_all /* cast away const */
        }
};

const prod_class_t _clss_none = {
        {0, 0}, /* TS_ZERO */
        {0, 0}, /* TS_ZERO */
        {
                0,
                (prod_spec *)&_spec_none /* cast away const */
        }
};


/*
 * Boolean function to determine if two prod_specs
 * are "the same". Doesn't know reg exp syntax.
 */
int
spec_eq(prod_spec *left, prod_spec *rght)
{
        if(left == rght)
                return 1;
        if(left == NULL || rght == NULL)
                return 0;
        if(left->feedtype == rght->feedtype)
        {
                if(left->pattern == rght->pattern)
                        return 1;
                if(left->pattern == NULL || rght->pattern == NULL)
                        return 0;
                return(!strcmp(left->pattern, rght->pattern));
        }
        return 0;
}


/*
 * Common code for clsspsa_eq() and clss_eq() below.
 */
static int
psa_eq(const prod_class_t *lhs, const prod_class_t *rhs)
{
        /* assert(lhs != NULL && rhs != NULL); */

        if(lhs->psa.psa_len != rhs->psa.psa_len)
                return 0;

        {
                int ii = (int) rhs->psa.psa_len;
                while(--ii >= 0)
                {
                        if(!spec_eq(&lhs->psa.psa_val[ii],
                                         &rhs->psa.psa_val[ii]))
                                return 0;
                }
        }
        return 1;
}


/*
 * Boolean function to determine if psa arrays of
 * two product classes are are "the same".
 */
int
clsspsa_eq(const prod_class_t *lhs, const prod_class_t *rhs)
{
        if(lhs == rhs)  
                return 1;

        if(lhs == NULL || rhs == NULL)
                return 0;
        
        return psa_eq(lhs, rhs);
}



/*
 * Boolean function to determine if two prod_classes are "the same".
 */
int
clss_eq(const prod_class_t *lhs, const prod_class_t *rhs)
{
        if(lhs == rhs)  
                return 1;

        if(lhs == NULL || rhs == NULL)
                return 0;

        if(!tvEqual(lhs->from, rhs->from))
                return 0;
        if(!tvEqual(lhs->to, rhs->to))
                return 0;
        if(lhs->psa.psa_len != rhs->psa.psa_len)
                return 0;

        return psa_eq(lhs, rhs);
}


/*
 * Boolean function to determine whether the timestamp
 * 'tsp' is in the time range of 'clssp'
 */
int
timeInClass(const prod_class_t *clssp, const timestampt *tsp)
{
    return
        clssp == NULL || tsp == NULL
            ? 0
            : clss_eq(clssp, PQ_CLASS_ALL)
                ? 1
                : tvCmp(clssp->from, clssp->to, >)
                    ? !(tvCmp(*tsp, clssp->to, <)
                        || tvCmp(*tsp, clssp->from, >))
                    : !(tvCmp(*tsp, clssp->from, <)
                        || tvCmp(*tsp, clssp->to, >));
}


/*
 * Boolean function to determine whether
 * 'info' is in 'clssp'
 */
int
prodInClass(const prod_class_t *clssp, const prod_info *info)
{
        prod_spec *psp;

        if(clssp == PQ_CLASS_ALL)
                return 1;


        if(!timeInClass(clssp, &info->arrival)) {
                return 0;
        }
        /* else, It's in the time range */

        for(psp = clssp->psa.psa_val;
                psp < (&clssp->psa.psa_val[clssp->psa.psa_len]);
                psp ++)
        {
                if(info->feedtype & psp->feedtype)
                {
                        if(psp->pattern != NULL
                                 && (strcmp(_spec_all.pattern, psp->pattern)
                                         == 0))
                        {
                                /* pattern is ".*" */
                                return 1;
                        }
                        if(regexec(&psp->rgx,
                                        info->ident, 0, NULL, 0) == 0)
                        {
                                /* it matches */
                                return 1;
                        }
                }
        }
        /* no match */
        return 0;
}


int
cp_prod_spec(prod_spec *lhs, const prod_spec *rhs)
{
        assert(rhs != NULL);
        assert(lhs != NULL);
        assert(rhs->pattern != NULL);
        assert(lhs->pattern == NULL);

        lhs->feedtype = rhs->feedtype;
        lhs->pattern = strdup(rhs->pattern);
        if(lhs->pattern == NULL)
                return errno; /* out of memory */
        /* N.B. Always compiles the pattern into rgx */
        (void)re_vetSpec(lhs->pattern);
        if(regcomp(&lhs->rgx, lhs->pattern, REG_EXTENDED) != 0)
                return EINVAL; /* couldn't compile pattern */
        return ENOERR;
}


void
free_prod_class(prod_class_t *clssp)
{
        if(clssp == NULL)
                return;

        if (clssp == &_clss_all)
            return;

        if(clssp->psa.psa_val != NULL)
        {
                int ii = (int) clssp->psa.psa_len;
                while(--ii >= 0)
                {
                        if(clssp->psa.psa_val[ii].pattern != NULL)
                        {
                                free(clssp->psa.psa_val[ii].pattern);
                                clssp->psa.psa_val[ii].pattern = NULL;
                                regfree(&clssp->psa.psa_val[ii].rgx);
                                (void)memset(&clssp->psa.psa_val[ii].rgx,
                                        0, sizeof(regex_t));
                        }
                }
                
                if(clssp->psa.psa_val != (prod_spec *)(&clssp[1]))
                {
                        /* allocated separately */
                        free(clssp->psa.psa_val);
                }
        }

        
        free(clssp);
}


/*
 * Returns a new product-class structure.
 *
 * Arguments:
 *      psa_len         The size of the product-specification array.
 * Returns:
 *      NULL            Out-of-memory. "log_start()" called.
 *      else            Pointer to the new product-class structure. The length
 *                      of the product-spcficiation array will be set but its
 *                      elements will be NULL.
 */
prod_class_t *
new_prod_class(
    unsigned            psa_len)
{
    prod_class_t*       clssp;
    size_t              sz = sizeof(prod_class_t) + psa_len *
        sizeof(prod_spec);   

    clssp = (prod_class_t*)malloc(sz);

    if (clssp == NULL) {
        LOG_SERROR1("Couldn't allocate %lu bytes for product-class", sz);
    }
    else {
        (void)memset(clssp, 0, sz);

        clssp->psa.psa_val = (prod_spec*)(&clssp[1]);
        clssp->psa.psa_len = psa_len;

        {
            unsigned    i;
            for (i = 0; i < psa_len; ++i)
                clssp->psa.psa_val[i].pattern = NULL;
        }
    }

    return clssp;
}


/*
 * Copies a product-class.  The size of the product-specification array of
 * *lhs must be greater than or equal to that of *rhs (e.g. use lhs =
 * new_prod_class(rhs->psa.psa_len)).
 *
 * Arguments:
 *      lhs             The product-class into which to copy.
 *      rhs             The product-class to be copied.
 *      shallow         If true, then only the feed-types of the
 *                      feed-type/pattern product-specifications will be
 *                      copied; otherwise, both the feed-types and patterns
 *                      will be copied.
 * Returns:
 *      0               if successful.
 *      EINVAL          if the regular expression pattern of a
 *                      product-specification couldn't be compiled.
 *                      "log_start()" called.
 *      ENOMEM          if out-of-memory. "log_start()" called.
 * Throws:
 *      SIGSEGV         if "lhs" or "rhs"  is NULL.
 */
int
cp_prod_class(
    prod_class_t*       lhs,
    const prod_class_t* rhs,
    const int           shallow)
{
    int                 status = ENOERR;

    assert(rhs != NULL);
    assert(lhs != NULL);
    
    lhs->from = rhs->from;
    lhs->to = rhs->to;

    for (lhs->psa.psa_len = 0; lhs->psa.psa_len < rhs->psa.psa_len;
            lhs->psa.psa_len++) {
        if (!shallow) {
            char        buf[80];
            prod_spec*  prodSpec = &rhs->psa.psa_val[lhs->psa.psa_len];

            status = cp_prod_spec(&lhs->psa.psa_val[lhs->psa.psa_len],
                prodSpec);

            if (status != ENOERR) {
                LOG_SERROR1("Couldn't copy product-specification \"%s\"",
                        sprint_prod_spec(buf, sizeof(buf), prodSpec));
                return status;
            }
        }
        else {
            lhs->psa.psa_val[lhs->psa.psa_len].feedtype =
                    rhs->psa.psa_val[lhs->psa.psa_len].feedtype;
        }
    }

    assert(lhs->psa.psa_len ==  rhs->psa.psa_len);

    return status;
}


/*
 * Returns a deep copy of a product-class.
 *
 * Arguments:
 *      class           Pointer to the product-class to be duplicated.
 *
 * Returns:
 *      NULL            Out-of-memory. "log_start()" called.
 *      else            Pointer to the duplicate product-class.
 */
prod_class_t *
dup_prod_class(
    const prod_class_t* class)
{
    prod_class_t *clone = new_prod_class(class->psa.psa_len);

    if (clone == NULL) {
        LOG_ADD0("Couldn't allocate product-class clone");
    }
    else {
        if (cp_prod_class(clone, class, 0)) {
            LOG_ADD0("Couldn't copy product-class to clone");
            free_prod_class(clone);
            clone = NULL;
        }
    }

    return clone;
}


void
clss_scrunch(prod_class_t *clssp)
{
        const int len = (int) clssp->psa.psa_len;
        int ii;
        prod_spec *sp, *end;

        assert(clssp != NULL);
        
        sp = clssp->psa.psa_val;
        end = &clssp->psa.psa_val[len -1];
        for(ii = 0 ; ii < len; ii++)
        {
                if(sp->feedtype == NONE)
                {
                        if(sp->pattern != NULL)
                        {
                                free(sp->pattern);
                                sp->pattern = NULL;
                                regfree(&sp->rgx);
                                (void)memset(&sp->rgx, 0, sizeof(regex_t));
                        }
                        if(sp != end)
                        {
                                /* shuffle */
                                prod_spec *next = sp + 1;
                                (void)memcpy(sp, next, 
                                    (size_t)((char*)(end +1) - (char *)next));
                                (void)memset(end, 0, sizeof(prod_spec));
                        }
                        end--;
                        clssp->psa.psa_len--;
                }
                else
                        sp++;
        }
}


int
clss_intersect(const prod_class_t *filt, const prod_class_t *want,
        prod_class_t **clsspp)
{
        int status = ENOERR;
        prod_class_t *is;

        if(filt != want && (filt == NULL || want == NULL))
        {
                *clsspp = NULL;
                return status;
        }
        
        if(filt->psa.psa_len == 0 || want->psa.psa_len == 0)
        {
                *clsspp = NULL;
                return status;
        }
        if(tvEqual(filt->from, filt->to) || tvEqual(want->from, want->to))
        {
                *clsspp = NULL;
                return status;
        }

        
        is = new_prod_class(want->psa.psa_len);
        if(is == NULL)
                return errno;

        status = cp_prod_class(is, want, 0);
        if(status != ENOERR)
                return status;

        /* else */

        if(clss_eq(filt, PQ_CLASS_ALL)
                        || filt == want)
        {
                /* done */
                *clsspp = is;
                return ENOERR;
        }

        if(!tvEqual(filt->from, TS_ZERO) || !tvEqual(filt->to, TS_ENDT))
        {
                /* Begin time intersect block */
                timestampt lf = filt->from;
                timestampt lt = filt->to;
                timestampt rf = want->from;
                timestampt rt = want->to;
                int wantswapped = 0;

                if(tvCmp(lf, lt, >))
                {
                        swap_timestamp(&lf, &lt);
                }
                
                if(tvCmp(rf, rt, >))
                {
                        swap_timestamp(&rf, &rt);
                        wantswapped = 1;
                }
                /*
                 * "from" and "to" now in ascending order
                 * for both filt and want
                 */

                if(tvCmp(lt, rf, <) || tvCmp(rt, lf, <))
                {
                        /* disjoint time intervals */
                        goto unwind_alloc;
                }
                /* else */

                if(tvCmp(lf, rf, >))
                        is->from = lf;
                else
                        is->from = rf;

                if(tvCmp(lt, rt, <))
                        is->to = lt;
                else
                        is->to = rt;
                
                /*
                 * We use the scan order of the second argument
                 * as the order of the result.
                 */
                if(wantswapped)
                        swap_timestamp(&is->from, &is->to);

        } /* End time intersect block */

        if(filt->psa.psa_val[0].feedtype != ANY)
        {
                unsigned int ii, jj;
                feedtypet fi;
                for(ii = 0; ii < is->psa.psa_len; ii++)
                {
                        for(jj = 0; jj < filt->psa.psa_len; jj++)
                        {
                                fi = filt->psa.psa_val[jj].feedtype
                                        & is->psa.psa_val[ii].feedtype;
                                if(fi)
                                {
                                        is->psa.psa_val[ii].feedtype = fi;
                                        break; /* first match priority */
                                }
                        }       
                }
        }

        *clsspp = is;
        return ENOERR;

unwind_alloc:
        free_prod_class(is);
        return status;
}

/**
 * Removes from an initial product class all product specifications in another
 * product class. The time limits in the initial product class are untouched.
 *
 * @param initial       [in/out] Initial product class
 * @param remove        [in] Product specifications to be removed
 */
void clss_remove_prod_specs(
        prod_class_t* const initial,
        const prod_class_t* const remove)
{
    int i;

    for (i = 0; i < initial->psa.psa_len; i++) {
        prod_spec* init_ps = initial->psa.psa_val + i;
        int j;

        for (j = 0; j < remove->psa.psa_len; j++) {
            prod_spec* rem_ps = remove->psa.psa_val + i;

            if ((init_ps->feedtype & rem_ps->feedtype) &&
                    strcmp(init_ps->pattern, rem_ps->pattern) == 0) {
                init_ps->feedtype &= ~rem_ps->feedtype;
            }
        }
    }

    clss_scrunch(initial);
}


/**
 * Compiles all product-identifier patterns in a product-class.
 *
 * @param[in] clssp  The product-class.
 */
void
clss_regcomp(prod_class_t *clssp)
{
        unsigned int ii;
        if(clssp == NULL 
                        || clssp->psa.psa_val == NULL
                        || clssp->psa.psa_len == 0)
                return;
        /* else */
        for(ii = 0; ii < clssp->psa.psa_len; ii++)
        {
                (void)re_vetSpec(clssp->psa.psa_val[ii].pattern);
                regcomp(&clssp->psa.psa_val[ii].rgx,
                        clssp->psa.psa_val[ii].pattern, REG_EXTENDED);
        }
}


feedtypet
clss_feedtypeU(const prod_class_t *clssp)
{
        unsigned int ii;
        feedtypet feedtypeU = NONE;
        
        if(clssp != NULL && clssp->psa.psa_val != NULL)
        {
                for(ii = 0; ii < clssp->psa.psa_len; ii++)
                {
                        feedtypeU |= clssp->psa.psa_val[ii].feedtype; 
                }
        }
        return feedtypeU;
}

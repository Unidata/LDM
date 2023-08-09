/**
 *   Copyright 2013, University Corporation for Atmospheric Research
 *   All rights reserved.
 *   <p>
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 *   <p>
 *   This file accumulates statistics for the rtstats(1) program and reports
 *   them to an LDM server.
 */

#include <config.h>
#include <limits.h> /* PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 255
#endif /* !PATH_MAX */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>

#include "log.h"
#include "ldm.h"
#include "ldmalloc.h"
#include "ldmprint.h"
#include "atofeedt.h"
#include "inetutil.h"
#include "timestamp.h"

extern int  ldmsend_connect(void);
extern int  ldmsend_send(char *statsdata, const char* hostname);
extern void ldmsend_disconnect(void);

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL        60
#endif

#define DEFAULT_RANDOM          (DEFAULT_INTERVAL/2)

typedef struct statsbin {
        int needswrite;
        time_t interval;
        timestampt recent;  /* infop->arrival most recent */
        timestampt recent_a; /* reftime (queue time) most recent */
        feedtypet feedtype;
        char origin[HOSTNAMESIZE];
        double nprods;        
        double nbytes;        
        double latency_sum;     
        double max_latency;
        time_t slowest_at;
} statsbin;


/* when you need more, grow by this amount */
#define NGROW   8 
/*
 * Max size of the list.
 * This needs to be big enough that the oldest is beyond any conceivable
 * latency, yet small enough that you can sort it and don't mind
 * the memory usage. It also needs to be a multiple of NGROW.
 */
#define MAXBINS (500*NGROW)
/* number allocated, grows to reach MAXBINS */
static size_t maxbins = 0;
/* number in use, <= maxbins */
static size_t nbins = 0;
static statsbin **binList = NULL;


static char *
s_time(char *buf, size_t bufsize, time_t when)
{
        
        struct tm tm_arriv;

#define P_TIMET_LEN 15 /* YYYYMMDDHHMMSS\0 */
        if(!buf || bufsize < P_TIMET_LEN)
                return buf;

        (void) memset(buf, 0, bufsize);
        tm_arriv = *(gmtime(&when));
        (void)strftime(buf, bufsize,
                        "%Y%m%d%H%M%S", &tm_arriv);
        return buf;
}


static char *
s_time_abrv(time_t when)
{
        
        static char buf[32];
        struct tm tm_arriv;

        (void) memset(buf, 0, sizeof(buf));
        tm_arriv = *(gmtime(&when));
        (void)strftime(buf, sizeof(buf),
                        "%M%S", &tm_arriv);
        return buf;
}


static void
dump_statsbin(statsbin *sb)
{
        char buf[P_TIMET_LEN];
        char buf_a[P_TIMET_LEN];

        log_notice_q("%s %s %s %12.0lf %12.0lf %10.2f %4.0f@%s %s",
                s_time(buf, sizeof(buf), sb->recent.tv_sec),
                s_feedtypet(sb->feedtype),
                sb->origin,
                sb->nprods,
                sb->nbytes,
                sb->latency_sum/(sb->nprods == 0 ? 1 : sb->nprods),
                sb->max_latency,
                s_time_abrv(sb->slowest_at),
                s_time(buf_a, sizeof(buf_a), sb->recent_a.tv_sec)
        );

}


/**
 * Reports statistics to an LDM server.
 *
 * @param sb            [in] The statistics to be sent.
 * @param myname        [in] The name of the local host.
 * @retval 0            Success or nothing to report.
 * @retval -1           The LDM server couldn't be contacted. An error-message is logged.
 * @retval ENOMEM       Out-of-memory.
 * @retval ECONNABORTED The transmission attempt failed for some reason.
 */
static int
binstats_report(
        statsbin*           sb,
        const char* const   myname)
{
        char buf[P_TIMET_LEN];
        char buf_a[P_TIMET_LEN];
        char stats_data[4096];
        int  status;

        if(sb->recent_a.tv_sec == -1) {
            status = 0;
        }
        else {
            int nbytes = snprintf(stats_data, sizeof(stats_data),
                    "%14.14s %14.14s %32.*s %7.10s %32.*s %12.0lf %12.0lf %.8g %10.2f %4.0f@%4.4s "
                        "%20.20s\n",
                    s_time(buf, sizeof(buf), sb->recent.tv_sec),
                    s_time(buf_a, sizeof(buf_a), sb->recent_a.tv_sec),
                    (int)_POSIX_HOST_NAME_MAX,
                    myname,
                    s_feedtypet(sb->feedtype),
                    (int)HOSTNAMESIZE,
                    sb->origin,
                    sb->nprods,
                    sb->nbytes,
                    d_diff_timestamp(&sb->recent_a, &sb->recent),
                    sb->latency_sum/(sb->nprods == 0 ? 1: sb->nprods),
                    sb->max_latency,
                    s_time_abrv(sb->slowest_at),
                    PACKAGE_VERSION
            );
            status = ldmsend_send(stats_data, myname);
            if (status == 0)
                sb->needswrite = 0;
        }

        return status;
}


static int
fscan_statsbin(FILE *fp, statsbin *sb)
{
        int nf;
        struct tm interval_tm;
        struct tm file_tm;
        char feedtype_str[32];
        double mean;
        int min;
        int sec;

        (void)memset(&file_tm, 0, sizeof(file_tm));

        nf = fscanf(fp, "%4d%2d%2d%2d%2d%d2",
                &file_tm.tm_year,
                &file_tm.tm_mon,
                &file_tm.tm_mday,
                &file_tm.tm_hour,
                &file_tm.tm_min,
                &file_tm.tm_sec
        );
        if(nf != 6) 
                return EOF;
        /* correct to tm representation */
        file_tm.tm_year -= 1900;
        file_tm.tm_mon--;

        sb->recent.tv_sec = mktime(&file_tm); /* N.B. TZ must be UTC */
        sb->recent.tv_usec = 0;

        interval_tm = file_tm;
        interval_tm.tm_sec = 0;
        interval_tm.tm_min = 0;
        sb->interval =  mktime(&interval_tm); /* N.B. TZ must be UTC */

        nf = fscanf(fp, "%31s %63s %15lf %15lf %15lf %15lf@%2d%2d",
                feedtype_str,
                sb->origin,
                &sb->nprods,
                &sb->nbytes,
                &mean,
                &sb->max_latency,
                &min,
                &sec
        );
        if(nf != 8) 
                return EOF;
        sb->feedtype = atofeedtypet(feedtype_str);
        sb->latency_sum = mean * sb->nprods;
        sb->slowest_at = sb->interval + 60*min + sec;

        nf = fscanf(fp, "%4d%2d%2d%2d%2d%d2",
                &file_tm.tm_year,
                &file_tm.tm_mon,
                &file_tm.tm_mday,
                &file_tm.tm_hour,
                &file_tm.tm_min,
                &file_tm.tm_sec
        );
        if(nf == 6)
        {
                /* correct to tm representation */
                file_tm.tm_year -= 1900;
                file_tm.tm_mon--;
                /*
                 * N.B. TZ must be UTC
                 */
                sb->recent_a.tv_sec = mktime(&file_tm);
                sb->recent_a.tv_usec = 0;
        }

        sb->needswrite = 0;

        return nf;
}


static void
free_statsbin(statsbin *sb)
{
        if(sb == NULL)
                return;
        free(sb);
}


static int
init_statsbin(statsbin *sb, time_t interval, feedtypet feedtype, char *origin)
{
        if(sb == NULL)
                return -1;
        (void) memset(sb, 0, sizeof(statsbin));
        sb->interval = interval;
        sb->feedtype = feedtype;
        if(origin && *origin)
                strncpy(sb->origin, origin, HOSTNAMESIZE -1);
        return 0;
}


static statsbin *
new_statsbin(time_t interval, feedtypet feedtype, char *origin)
{
        statsbin *sb;
        if(interval == 0 || feedtype == NONE)
                return NULL;
        sb = Alloc(1, statsbin);                
        if(sb == NULL)
                return NULL;
        init_statsbin(sb, interval, feedtype, origin);

        return sb;
}


static int
node_compare(const void *p1, const void *p2)
{
        statsbin *h1 = *((statsbin **)p1);
        statsbin *h2 = *((statsbin **)p2);

        if(h2 == NULL || h2->interval == 0)
                return 1;
        if(h1 == NULL || h1->interval == 0)
                return -1;

        if(h1->interval > h2->interval)
                return -1;
        if(h1->interval < h2->interval)
                return 1;
        /* else, same interval */

        if(h1->feedtype < h2->feedtype)
                return -1;
        if(h1->feedtype > h2->feedtype)
                return 1;
        /* else, same feedtype */

        if(*h2->origin == 0)
                return 1;
        if(*h1->origin == 0)
                return -1;
        return strcasecmp(h1->origin, h2->origin);      
}


static size_t
growlist(void)
{
        if(maxbins == 0)
        {
                /* first time */
                binList = Alloc(NGROW, statsbin *);
                if(binList == NULL)
                        return 0;
                /* else */
                maxbins = NGROW;
                return maxbins;
        } 
        /* else */
        if(nbins <= MAXBINS)
        {
                binList = (statsbin **)realloc(binList,
                                (maxbins + NGROW) * sizeof(statsbin *));
                if(binList == NULL)
                {
                        /* !??? */
                        maxbins = 0;
                        return 0;
                }
                /* else */
                maxbins += NGROW;
                return  maxbins;
        }
        /* else, recycle */
        while(nbins > MAXBINS)
        {
                nbins--;
                free_statsbin(binList[nbins]);
                binList[nbins] = 0;
        }
        return  maxbins;
}


void
fromfile(FILE *fp)
{
        /* attempt to initialize from existing file */
        statsbin fsb;
        statsbin *sb;

        rewind(fp);
        (void) memset(&fsb, 0, sizeof(fsb));
        while(fscan_statsbin(fp, &fsb) != EOF)
        {
                if(nbins > 0)
                {
                        statsbin *keyp, **sbp;
                        keyp = &fsb;
                        sbp = (statsbin **)bsearch(&keyp, binList, nbins,
                                sizeof(keyp), node_compare);
                        if(sbp != NULL && *sbp != NULL)
                                continue; /* found this entry,=> already read */
                }
                if(nbins >= maxbins)
                {
                        if(growlist() < nbins)
                        {
                                break;
                        }
                }
                /* tack it on the end of the list */
                sb = Alloc(1, statsbin);                
                if(sb == NULL)
                        return; /* out of memory */
                *sb = fsb;
                (void) memset(&fsb, 0, sizeof(fsb));
                binList[nbins++] = sb;  
                /* keep the list sorted */
                qsort(binList, nbins,
                        sizeof(statsbin *), node_compare);
        }
}


static statsbin *
get_statsbin(time_t interval, feedtypet feedtype, char *origin)
{
        statsbin *sb;

        if(nbins > 0)
        {
                statsbin key, *keyp, **sbp;
                if(init_statsbin(&key, interval, feedtype, origin) < 0)
                        return NULL;
                keyp = &key;
                sbp = (statsbin **)bsearch(&keyp, binList, nbins,
                        sizeof(keyp), node_compare);
                if(sbp != NULL && *sbp != NULL)
                        return *sbp; /* found it */
        }
        /* else */
        /* create a new entry */
        if(nbins >= maxbins)
        {
                if(growlist() < nbins)
                        return NULL; /* out of space */
        }
        sb = new_statsbin(interval, feedtype, origin);
        if(sb == NULL)
                return NULL;
        /* tack it on the end of the list */
        binList[nbins++] = sb;  
        /* keep the list sorted */
        qsort(binList, nbins,
                sizeof(statsbin *), node_compare);
        return sb;
}


static time_t
arrival2interval(time_t arrival)
{
        return ((arrival/3600)*3600);
}


void
binstats_dump(void)
{
        size_t ii;
        if(nbins == 0)
                return;
        for(ii = 0; ii < nbins ; ii++)
                dump_statsbin(binList[ii]);
}


int
binstats_add(
        const prod_info*      infop,
        const struct timeval* reftimep)
{
        statsbin *sb;
        double latency = d_diff_timestamp(reftimep, &infop->arrival);
        time_t interval = arrival2interval(infop->arrival.tv_sec);

        sb = get_statsbin(interval, infop->feedtype, infop->origin);
        if(sb == NULL)
                return -1;

        sb->nprods = sb->nprods + 1.0;
        sb->nbytes = sb->nbytes + (double)infop->sz;
        sb->recent = infop->arrival;
        sb->recent_a = *reftimep;
        sb->latency_sum += latency;

        if(latency > sb->max_latency)
        {
                sb->max_latency = latency;
                sb->slowest_at = infop->arrival.tv_sec;
        }
        
        sb->needswrite = 1;

        return 0;
}


/**
 * Sends a report to the downstream LDM if the time is right.
 *
 * @param[in] hostname  The name of the local host.
 */
void
binstats_sendIfTime(const char* const hostname)
{
    static time_t lastsent = 0;
    static int    reportGap = DEFAULT_INTERVAL;

    if (time(NULL) - lastsent >= reportGap) {
        if (ldmsend_connect() == 0) { // Logs message on error
           for (size_t ii = 0; ii < nbins; ++ii) {
               if (binList[ii]->needswrite && binstats_report(binList[ii], hostname)) {
                   log_flush_error();
                   break;
               }
           }

           ldmsend_disconnect();
        } // Connected to downstream LDM

        lastsent = time(NULL);

        // Add a random time offset to disperse the reporting times.
        const float rfact = (float)( random() & 0x7f ) / (float)(0x7f);
        reportGap = DEFAULT_INTERVAL + (int)(DEFAULT_RANDOM * rfact);
    } // Time to report
}

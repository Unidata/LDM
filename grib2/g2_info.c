#include <stdio.h>
#include <stdlib.h>
#include "grib2.h"
#include "ByteBuf.h"

g2int g2_info(unsigned char *cgrib, int sz, g2int *listsec0, g2int *listsec1,
            g2int *numfields, g2int *numlocal)
/*$$$  SUBPROGRAM DOCUMENTATION BLOCK
//                .      .    .                                       .
// SUBPROGRAM:    g2_info 
//   PRGMMR: Gilbert         ORG: W/NP11    DATE: 2002-10-28
//
// ABSTRACT: This subroutine searches through a GRIB2 message and
//   returns the number of gridded fields found in the message and
//   the number (and maximum size) of Local Use Sections.
//   Also various checks  are performed
//   to see if the message is a valid GRIB2 message.
//
// PROGRAM HISTORY LOG:
// 2002-10-28  Gilbert
// ????-??-??  S. Chiswell modified to pass message size to ensure reading
//             doesn't go past message length
// 2013-05-28  Steve Emmerson modified to increase the level of verification of
//             the GRIB2 message structure.
//
// USAGE:   int g2_info(unsigned char *cgrib, g2int sz, g2int *listsec0,
//            g2int *listsec1, g2int *numfields, g2int *numlocal)
//   INPUT ARGUMENTS:
//     cgrib    - Character pointer to the GRIB2 message. May have junk in the
//                first 100 bytes before the "GRIB" sentinel and may have junk
//                after the valid GRIB message.
//     sz       - Length of "cgrib" data in bytes
//
//   OUTPUT ARGUMENTS:      
//     listsec0 - pointer to an array containing information decoded from 
//                GRIB Indicator Section 0.
//                Must be allocated with >= 3 elements.
//                listsec0[0]=Discipline-GRIB Master Table Number
//                            (see Code Table 0.0)
//                listsec0[1]=GRIB Edition Number (currently 2)
//                listsec0[2]=Length of GRIB message
//     listsec1 - pointer to an array containing information read from GRIB 
//                Identification Section 1.
//                Must be allocated with >= 13 elements.
//                listsec1[0]=Id of originating centre (Common Code Table C-1)
//                listsec1[1]=Id of originating sub-centre (local table)
//                listsec1[2]=GRIB Master Tables Version Number (Code Table 1.0)
//                listsec1[3]=GRIB Local Tables Version Number 
//                listsec1[4]=Significance of Reference Time (Code Table 1.1)
//                listsec1[5]=Reference Time - Year (4 digits)
//                listsec1[6]=Reference Time - Month
//                listsec1[7]=Reference Time - Day
//                listsec1[8]=Reference Time - Hour
//                listsec1[9]=Reference Time - Minute
//                listsec1[10]=Reference Time - Second
//                listsec1[11]=Production status of data (Code Table 1.2)
//                listsec1[12]=Type of processed data (Code Table 1.3)
//     numfields- The number of gridded fields found in the GRIB message.
//                That is, the number of occurrences of Sections 4 - 7.
//     numlocal - The number of Local Use Sections ( Section 2 ) found in 
//                the GRIB message.
//
//     RETURN VALUES:
//     ierr     - Error return code.
//                0 = no error
//                1 = Beginning characters "GRIB" not found.
//                2 = GRIB message is not Edition 2.
//                3 = Could not find Section 1 where expected.
//                4 = End string "7777" found prematurely
//                5 = End string "7777" not found at end of message.
//                6 = Invalid section number found.
//                7 = Invalid total or section length parameter in message
//                8 = Corrupt GRIB message
//
// ATTRIBUTES:
//   LANGUAGE: C
//   MACHINE:  
//
//$$$*/
{
      g2int     lengrib;
      g2int     isecnum;
      g2int     lensec;
      ByteBuf   bb;
      size_t    sectionStart;
      size_t    remaining;

      *numlocal=0;
      *numfields=0;

     /*
      *  Check for beginning of GRIB message in the first 100 bytes
      */
      bb_init(&bb, cgrib, sz);
      if (bb_find(&bb, "GRIB", 100))
          return 1;
      sectionStart = bb_getCursor(&bb); /* beginning of GRIB message */
      remaining = bb_getRemaining(&bb);

      /*
       *  Unpack Section 0 - Indicator Section
       */
      if (bb_skip(&bb, 6))
          return 8;
      if (bb_getInt(&bb, 1, listsec0+0))
          return 8;
      if (bb_getInt(&bb, 1, listsec0+1))
          return 8;
      if (bb_skip(&bb, 4))
          return 8;
      if (bb_getInt(&bb, 4, &lengrib))
          return 8;
      listsec0[2]=lengrib;

      /*
       * Validate length of GRIB message. S. Chiswell & S. Emmerson.
       */
      if (lengrib > remaining) {
        printf("g2_info: Sanity check, grib length larger than amount of data: "
                "lengrib=%ld, actual=%lu\n", (long)lengrib,
                (unsigned long)remaining);
        return 7;
      }

      /*
       *  Currently handles only GRIB Edition 2.
       */
      if (listsec0[1] != 2) {
        printf("g2_info: can only decode GRIB edition 2.\n");
        return 2;
      }

      /*
       *  Unpack Section 1 - Identification Section
       */
      sectionStart += 16; /* start of section 1 */
      if (bb_setCursor(&bb, sectionStart))
          return 8;

      /*
       * Validate length of section 1. S. Emmerson.
       */
      remaining = bb_getRemaining(&bb);
      if (bb_getInt(&bb, 4, &lensec))
          return 8;
      if (16 > lensec || remaining < lensec) {
        printf("g2_info: Sanity check, section 1 length too big or too small: "
                "lensec=%ld, remaining=%lu\n", (long)lensec,
                (unsigned long)remaining);
        return 7;
      }

      /*
       * Validate the section number.
       */
      if (bb_getInt(&bb, 1, &isecnum))
          return 8;
      if (isecnum != 1) {
        printf("g2_info: Could not find section 1.\n");
        return 3;
      }

      {
          /*
           * Set the values in array "listsec1" from the number of bytes
           * specified in array "mapsec1".
           */
          int i;
          static const g2int        mapsec1[]={2,2,1,1,1,2,1,1,1,1,1,1,1};

          for (i=0; i < sizeof(mapsec1)/sizeof(mapsec1[0]); i++) {
            if (bb_getInt(&bb, mapsec1[i], listsec1+i))
              return 8;
          }
      }

      /*
       * Advance to the next section.
       */
      sectionStart += lensec;
      if (bb_setCursor(&bb, sectionStart))
        return 8;

      /*
       * Loop through the remaining sections to see if they are valid. Also
       * count the number of times Section 2 and Section 4 appear.
       */
      while (bb_getRemaining(&bb) >= 4) {
        g2int sevens[4];

        /*
         * Look for the sentinel section indicating the end of the GRIB
         * message.
         */
        if (bb_getInts(&bb, 1, 4, sevens))
            return 8;
        if (sevens[0]=='7' && sevens[1]=='7' && sevens[2]=='7' &&
                sevens[3]=='7') {
          if (bb_getRemaining(&bb)) {
            printf("g2_info: '7777' found, but not where expected.\n");
            return 4;
          }
          return 0;
        }

        /*
         * Reset the byte-buffer to the beginning of this section.
         */
        bb_setCursor(&bb, sectionStart);

        /*
         * Validate this section's length.
         */
        remaining = bb_getRemaining(&bb);
        if (bb_getInt(&bb, 4, &lensec))
            return 8;
        if (5 > lensec || remaining < lensec) {
          printf("g2_info: Sanity check, section length too big or too small: "
                  "lensec=%ld, remaining=%lu\n", (long)lensec,
                  (unsigned long)remaining);
          return 7;
        }

        /*
         * Validate this section's number.
         */
        if (bb_getInt(&bb, 1, &isecnum))
            return 8;
        if ( isecnum>=2 && isecnum<=7 ) {
           if (isecnum == 2) {    /* Local Section 2*/
              /*   increment counter for total number of local sections found*/
              (*numlocal)++;
           }
           else if (isecnum == 4) {
              /*   increment counter for total number of fields found*/
              (*numfields)++;
           }
        }
        else {
           printf("g2_info: Invalid section number found in GRIB message: "
                   "%ld\n", (long)isecnum);
           return 6;
        }

        /*
         * Advance to the start of the next section.
         */
        sectionStart += lensec;
        if (bb_setCursor(&bb, sectionStart))
            return 8;
      } /* while sufficient bytes remain */

      if (bb_getRemaining(&bb)) {
        printf("g2_info: '7777'  not found at end of GRIB message.\n");
        return 5;
      }

      return 0;

}

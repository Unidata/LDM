/*
 *   Copyright 2004, University Corporation for Atmospheric Research
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>



static const char *
s_byte(unsigned char bb)
{
        static char buf[4];
        memset(buf, 0, sizeof(buf));
        sprintf(buf, "%d", bb);
        return buf;
}

static const char *
s_model_id(const char *model, unsigned char bb)
{
        static char buf[10];
        memset(buf,0,sizeof(buf));
        sprintf(buf,"%s_%d",model,bb);
        return buf;
}

const char *
s_pds_center(unsigned char center, unsigned char subcenter)
{
        switch (center) {
        case 7: return "ncep";
	case 8: return "nwstg";
        case 9: return "nws";
	case 54: return "cms";
        case 74: return "ukmet";
        case 98: return "ecmwf";
        case 58: return "noc";
        case 59: return "fsl";
        case 60: return "ncar";
        }
        /* default */
        return s_byte(center);
}

const char *
s_pds_model(unsigned char center, unsigned char model)
{
        /* TODO: what are the NWS abbrevs of these ?*/
if(center == 7)
   {
        switch (model) {
	case   2: return "UVI";
	case   3: return "TRANS_DISP";
	case   4: return "SMOKE";
        case   5: return "SAT"; /* TODO, use pds octet 41 */
        case  10: return "NOW";
        case  11: return "GMGWM";
        case  12: return "SURGE";
        case  19: return "LFM";
        case  25: return "SNO";
	case  30: return "FORECASTER";
        case  39: return "NGM";
        case  42: return "GOI_GFS";
        case  43: return "GOI_FNL";
        case  44: return "SST";
        case  45: return "OCN";
	case  46: return "HYCOM_GLOBAL";
	case  47: return "HYCOM_NPAC";
	case  48: return "HYCOM_NATL";
        case  53: return "LFM4";
        case  64: return "ROI";
        case  68: return "SPEC_80_GFS";
        case  69: return "SPEC_80_MRF";
        case  70: return "QLM";
        case  73: return "FOG";
        case  74: return "WWMEX";
        case  75: return "WWAK";
        case  76: return "BMRF";
        case  77: return "GFS";
        case  78: return "MRF";
        case  79: return "BACKUP";
        case  80: return "SPEC62MRF";
        case  81: return "SSIGFS";
        case  82: return "SSIGDAS";
        case  84: return s_model_id("NAM",model);
        case  86: return "RUC";
        case  87: return "ENSMB";
        case  88: return "NWW3";
        case  89: return s_model_id("NMM",model);
	case  94: return "MRF";
        case  96: return "GFS";
        case  98: return "CFS";
        case  99: return "TEST_ID";
        case 100: return "RSAS";
        case 101: return "RSAS";
        case 105: return "RUC2";
        case 107: return "GEFS";
        case 108: return "LAMP";
        case 109: return "RTMA";
        case 110: return s_model_id("NAM",model);
        case 111: return s_model_id("NAM",model);
        case 112: return "WRF_NMM";
        case 113: return s_model_id("SREF",model);
        case 114: return "NAEFS";
        case 115: return s_model_id("DGEX",model);
        case 116: return "WRF_EM";
        case 120: return s_model_id("ICE",model);
        case 121: return s_model_id("NWW",model);
        case 122: return s_model_id("NWW",model);
        case 123: return s_model_id("NWW",model);
        case 124: return s_model_id("NWW",model);
        case 125: return s_model_id("NWW",model);
        case 126: return "SEAICE";
        case 127: return "LAKEICE";
        case 128: return "GOCN";
        case 129: return "GODAS";
        case 130: return "MERGE";
        case 131: return "GLWM";
        case 140: return "NARR";
        case 141: return "LDAFS";
        case 150: return "RFS";
        case 151: return "FFGS";
        case 152: return "RAD2";
        case 153: return "RAD3";
	case 180: return "NCEP_QPF";
	case 181: return "RFC_QPF";
	case 182: return "RFC_QPE";
        case 183: return "NDFD";
        case 184: return "CCPA";
	case 190: return "AWC_NCWD";
	case 191: return "AWC_CIP";
        case 192: return "AWC_ANL";
        case 193: return "AWC_FCST";
	case 195: return "CDAS2";
	case 196: return "CDAS2";
	case 197: return "CDAS";
	case 198: return "CDAS";
	case 199: return "CFSR";
        case 200: return "CPC";
        case 201: return "CPC";
        case 210: return "EPA_AQ";
        case 211: return "EPA_AQ";
        case 215: return "SPC_MFP";
        case 220: return "NCEP_OPC";
        default:
                  return s_byte(model);
        }
   }

if((center == 8)||(center == 9))
   return s_model_id("NWS",model);

if(center == 98)
   return s_model_id("ECMWF",model);

if(center == 74)
   return s_model_id("UKM",model);

if(center == 59)
   {
   switch (model) {
        case 105: return "RUC2";
        default:
                  return s_model_id("FSL",model);
        }
   }

if(center == 54)
   {
   switch(model)
      {
      case  36: return "GEM";
      default:
		return s_model_id("CMS",model);
      }
   }

if(center == 60)
   return s_model_id("NCAR",model);

/* default */
return s_byte(model);
}

const char *
platform_id(unsigned char satid)
{
switch (satid) {
   case 2: return "MISC";
   case 3: return "JERS";
   case 4: return "ERS";
   case 5: return "POES";
   case 6: return "COMP";
   case 7: return "DMSP";
   case 8: return "GMS";
   case 9: return "METEOSAT";
   case 10: return "GOES-7";
   case 11: return "GOES-8";
   case 12: return "GOES-9";
   case 13: return "GOES-10";
   case 14: return "GOES-11";
   case 15: return "GOES-12";
   case 16: return "GOES-13";
   case 17: return "GOES-14";
   case 18: return "GOES-15";
   case 19: return "GOES-16";
   /* temporary POES numbers */
   case 27: return "POES";
   case 28: return "POES";
   }
/* default */
return s_byte(satid);
}

const char *channel_id(unsigned char channel)
{
switch (channel)
   {
   case 1: return "VIS";
   case 2: return "3.9";
   case 3: return "WV";
   case 4: return "IR";
   case 5: return "12.0";
   case 6: return "13.3";
   case 7: return "1.3";
   case 16: return "LI";
   case 17: return "PW";
   case 18: return "SFC-T";
   case 19: return "CAPE";
   case 27: return "CTP";
   case 28: return "CLD";
   case 29: return "PRXX";
   case 41: return "SOUND-14.71";
   case 42: return "SOUND-14.37";
   case 43: return "SOUND-14.06";
   case 44: return "SOUND-13.64";
   case 45: return "SOUND-13.37";
   case 46: return "SOUND-12.66";
   case 47: return "SOUND-12.02";
   case 48: return "SOUND-11.03";
   case 49: return "SOUND-9.71";
   case 50: return "SOUND-7.43";
   case 51: return "SOUND-7.02";
   case 52: return "SOUND-6.51";
   case 53: return "SOUND-4.57";
   case 54: return "SOUND-4.52";
   case 55: return "SOUND-4.45";
   case 56: return "SOUND-4.13";
   case 57: return "SOUND-3.98";
   case 58: return "SOUND-3.74";
   case 59: return "SOUND-VIS";
   case 61: return "VIS";
   case 63: return "3.74";
   case 64: return "11.0";
   }
return s_byte(channel);
}

const char *sector_id(unsigned char sector)
{
switch (sector)
   {
   case 0: return "NHEM-COMP";
   case 1: return "EAST-CONUS";
   case 2: return "WEST-CONUS";
   case 3: return "AK-REGIONAL";
   case 4: return "AK-NATIONAL";
   case 5: return "HI-REGIONAL";
   case 6: return "HI-NATIONAL";
   case 7: return "PR-REGIONAL";
   case 8: return "PR-NATIONAL";
   case 9: return "SUPER-NATIONAL";
   case 10: return "NHEM-MULTICOMP";
   }
return s_byte(sector);
}

int wmo_to_gridid (char *TT, char *AA )
{
int aval;

aval = AA[0] - 'A';
switch ( TT[0] )
   {
   case 'H':
	if ( ( aval >= 0 ) && ( aval < 26 ) )
	   {
	   int A1[]={21, 22, 23, 24, 25, 26, 50,
		     -1, 37, 38, 39, 40, 41, 42,
		     43, 44, -1, -1, -1, 61, 62,
		     63, 64, -1, -1, 255};
	   return ( A1[aval] );
           }
        else
           return(-1);
	break;
   case 'E':
   case 'O':
	if ( ( aval >= 0 ) && ( aval < 26 ) )
	   {
	   int A1[]={228, 218, 219, 220, 221, 229,
		     230, 231, 232, 233, 234, 235,
		     238, 239, 244, 251, 253, 212,
		     253, 214, 215, 216, 173, -1, -1, 255};
	   return ( A1[aval] );
           }
        else
           return(-1);
	break;
   case 'L':
   case 'M':
   case 'Y':
   case 'Z':
	if ( ( aval >= 0 ) && ( aval < 26 ) )
	   {
	   int A1[]={201, 218, 175, 130, 185, 186,
		     160, 213, 202, 203, 204, 205,
		     227, 207, 254, 237, 211, 212,
		     242, 161, 215, 216, 236, -1, 217, 255};
	   return ( A1[aval] );
           }
        else
           return(-1);
	break;
   default:
	return(-1);
   }
}

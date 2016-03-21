
/* ------------------------------------------------------------------------------
 *
 * File Name:
 *	goesr_lib.c
 *
 * Description:
 *	This file contains functions generating WMO headers based on file names.
 *
 * Functions defined:
 * 	findGoesrCmiNonMesoRegionID
 * 	findGoesrCmiMesoRegionID
 * 	calc_ii
 * 	calc_bbb
 * 	goesrCmiFile2Wmo
 *
 * Author:
 * 	Brian Rapp		Jun 29, 2012
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
 ------------------------------------------------------------------------------ */
#define _GNU_SOURCE

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include "../fileIngest/stdclib.h"
#include "../fileIngest/goesr_lib.h"
#include "../fileIngest/mlogger.h"

#define	FIELD_SEPARATOR			"_"
#define SUBFIELD_SEPARATOR		"-"
#define FIELD_COUNT			5
#define MAX_SCENE_NAME_LEN		32
#define DATE_STR_LEN			13
#define WMO_DATE_LEN			6
#define SIZE_LONG_WMO			25
#define MAX_FIELD_LEN			64
#define SIZE_RESOLUTION			3
#define SIZE_BITDEPTH			3
#define SUBFIELD3_SIZE			2
#define SIZE_LOCATION			7
#define SIZE_LAT			2
#define SIZE_LONG			3
#define SIZE_CHANNEL			2
#define SIZE_TILE			3
#define SIZE_BBB			3

#define WMO_T1_SATELLITE		'T'
#define WMO_T2_IMAGERY			'I'
#define WMO_A1_GOESR_CMI_NON_MESO	'R'
#define WMO_A1_GOESR_CMI_MESO		'S'
#define WMO_ORIGINATOR			"KNES"

#define	BASE				26
#define	MAX_SEQ_NUM			((BASE * BASE) - 1)

extern LOGGER		*eLog;

/* ------------------------------------------------------------------------------

Function Name:
	findGoesrCmiNonMesoRegionID

Synopsis:
	Calculates the A2 character of a GOES-R non-mesoscale product based on
	the region string.

Prototype:
	static char findGoesrCmiNonMesoRegionID (char *regionName)

Arguments:

	Name:		regionName
	Type:		char *
	Access:		read only

	Contains the region name from the GOES-R PD-generated file name.  Valid
	values are: AKRegi, EFD, WFD, EConus, HIRegi, PRRegi, WConus.

Description:
	This function finds the A2 character of a WMO header of the format
	"TTAAii CCCC YYGGgg bbb" using the region string in the file name of a
	non-mesoscale GOES-R file name.  Values are as follows:

	Region		A2
	======		==
	AKREGI		A
	TCONUS		C
	ECONUS		E
	HIREGI		H
	PRREGI		P
	EFD		S
	WFD		T
	TFD		U
	WCONUS		W

Return Values:
	A single character a defined above.  If a match is not found, an ASCII 'Z'
	is returned.

------------------------------------------------------------------------------ */

static char findGoesrCmiNonMesoRegionID (char *regionName) {

	int	i;

	struct a2_non_meso_table_struct {
		char	*regionName;
		char	regionDesignator;
	};
	static struct a2_non_meso_table_struct	a2_table[] = {
			{ "AKREGI",	'A' },
			{ "TCONUS",	'C' },
			{ "ECONUS",	'E' },
			{ "HIREGI",	'H' },
			{ "PRREGI",	'P' },
			{ "EFD",	'S' },
			{ "WFD",	'T' },
			{ "TFD",	'U' },
			{ "WCONUS",	'W' },
			{ "\0",		'\0' }
		};

	for (i = 0; a2_table[i].regionDesignator != '\0'; i++) {
		if (strncmp (regionName, a2_table[i].regionName, strlen (a2_table[i].regionName)) == 0)
			return (a2_table[i].regionDesignator);
	}

	return ('Z');
}

 /* ------------------------------------------------------------------------------

 Function Name:
 	findGoesrCmiMesoRegionID

 Synopsis:
 	Calculates the A2 character of a GOES-R mesoscale product based on the
 	the center point (lat/long) provided in the file name.

 Prototype:
 	static char findGoesrCmiMesoRegionID (int latitude, int longitude)

 Arguments:

 	Name:		latitude
 	Type:		int
 	Access:		read only

	Center point latitude.  Expected to be in the range -90 to +90.

 	Name:		longitude
 	Type:		int
 	Access:		read only

	Center point longitude.  Expected to be in the range -180 to 180.

 Description:
 	This function finds the A2 character of a WMO header of the format
 	"TTAAii CCCC YYGGgg bbb" using the center latitude/longitude of the
 	image as provided in the file name of a mesoscale GOES-R file name.

 	Mapping of the A2 values are determined according to the table below:


   	   90   180
       	       -180 -165 -150 -135 -120 -105 -90 -75 -60 -45 -30 -15  0
	75 +-----+--------------+-------------------------------------+ 75
   	   |     |              |                  T                  |
	60 |     |      R       |-------------------------------------+ 60
   	   |     |              | A  | B  | C | D | E |               |
	45 |     |--------------+----+----+---+---+---|               | 45
   	   |  V  |              | F  | G  | H | I | J |               |
	30 |     |              |----+----+---+---+---|       U       | 30
   	   |     |      S       | K  | L  | M | N | O |               |
	15 |     |              |-------------+-------|               | 15
   	   |     |              |      P      |   Q   |               |
	0  |----------------------------------------------------------+ 0
   	   |              Y               |             Z             |
	-90+------------------------------+---------------------------+ -90

	Eastern Longitudes are positive integers; Western are negative.  Similarly,
	Northern latitudes are positive while Southern are negative.

 Return Values:
 	A single character a defined above.  If a match is not found, an ASCII 'X'
 	is returned.

 ------------------------------------------------------------------------------ */

static char findGoesrCmiMesoRegionID (int latitude, int longitude) {

	 struct a2_meso_table_struct {
		 char	regionID;
		 int	top_lat;
		 int	bottom_lat;
		 int	left_long;
		 int	right_long;
	 };

	 const static struct a2_meso_table_struct	a2_table[] = {
		/*	   A2	       t.lat   b.lat   l.long  r.long	*/
			 { 'A',		60, 	45, 	-135, 	-120	},
			 { 'B',		60, 	45, 	-120, 	-105	},
			 { 'C',		60, 	45, 	-105, 	-90	},
			 { 'D',		60, 	45, 	-90, 	-75	},
			 { 'E',		60, 	45, 	-75, 	-60	},
			 { 'F',		45, 	30, 	-135, 	-120	},
			 { 'G',		45, 	30, 	-120, 	-105	},
			 { 'H',		45, 	30, 	-105, 	-90	},
			 { 'I',		45, 	30, 	-90, 	-75	},
			 { 'J',		45, 	30, 	-75, 	-60	},
			 { 'K',		30, 	15, 	-135, 	-120	},
			 { 'L',		30, 	15, 	-120, 	-105	},
			 { 'M',		30, 	15, 	-105, 	-90	},
			 { 'N',		30, 	15, 	-90, 	-75	},
			 { 'O',		30, 	15, 	-75, 	-60	},
			 { 'P',		15, 	0, 	-135, 	-90	},
			 { 'Q',		15, 	0, 	-90, 	-60	},
			 { 'R',		75, 	45, 	-180, 	-135	},
			 { 'S',		45, 	0, 	-180, 	-135	},
			 { 'T',		75, 	60, 	-135, 	0	},
			 { 'U',		60, 	0, 	-60, 	0	},
			 { 'V',		75, 	0, 	90, 	180	},
			 { 'Y',		0, 	-90, 	-180, 	-105	},
			 { 'Y',		0, 	-90, 	90, 	180	},
			 { 'Z',		0, 	-90, 	-105, 	0	},
			 { '\0',	0,	0,	0,	0	}
	 	 };

	 int		i;

	 for (i = 0; a2_table[i].regionID != '\0'; i++) {
		 if ((latitude >= a2_table[i].bottom_lat) && (latitude < a2_table[i].top_lat) &&
		     (longitude >= a2_table[i].left_long) && (longitude < a2_table[i].right_long)) {
			 return a2_table[i].regionID;
		 }
	 }

	 return ('X');
 }

/* ------------------------------------------------------------------------------

Function Name:
	calc_ii

Synopsis:
	Creates the ii from the channel number.


Prototype:
	static int calc_ii (int channel)

Arguments:

	Name:		channel
	Type:		unsigned int
	Access:		read only

	GOES-R ABI channel number for this scene.

Description:
	For now, this function is nothing more than a stub that returns the ABI
	channel number for ii.  If additional products are added later that
	requires a more complex calculation, this function will need to be
	modified.

Return Values:
	An integer value between 1 and 16 inclusive representing the 16 ABI
	channels.

------------------------------------------------------------------------------ */

static int calc_ii (int channel) {
	return channel;
}

/* ------------------------------------------------------------------------------

Function Name:
	calc_bbb

Synopsis:
	Creates the bbb field from the tile number.

Prototype:
	static char *calc_bbb (unsigned int sequence)

Arguments:

	Name:		sequence
	Type:		unsigned int
	Access:		read only

	GOES-R product tile number.

Description:
	The function calculates the optional BBB field of a WMO header using
	the tile number in the file name generated by the GOES-R ground segment
	for every file transmitted to AWIPS.

	The BBB field follows the Pxx indicator group specification with an
	exception for the final segment.  Because AWIPS has no a priori knowledge
	of the total number of tiles in a file and there is no indication within
	the file name, the BBB field for the final segment does not receive
	special treatment and follows the same calculation rules as every other
	tile.  The Pxx indicator group is calculated as follows:

	When a bulletin exceeds the length limit defined in Table A, it shall be
	segmented for communications purposes using a bulletin segment heading
	line utilizing the Pxx Indicator Group.  There are two different struc-
	tures possible for segments.  Segmented alphanumeric products have a
	supplementary identification line which repeats information in addition
	to the bulletin segment heading line in each segment. Segmented binary
	products do not repeat the supplementary identification line information
	in a second line for each subsequent segment. Only the bulletin segment
	heading line is repeated.

	Defining:

	Pxx = values of xx = AA through ZZ

	The following principles shall apply when segmenting alphanumeric
	bulletins for transmission:

	The first bulletin segment heading will have sequence indicator xx = AA,
	the second AB and so on.  The segment heading is a base 26 number using
	the letters of the alphabet to represent each digit.  This scheme allows
	26 * 26 or 676 possible combinations.  Since the first tile will be 1
	and the first segment must be AA, the sequence is first biased to 0
	before calculating the segment.

Return Values:
	This function has no return values.  The bbb is returned in the second
	argument, which must be allocated to at least 4 bytes by the caller.

------------------------------------------------------------------------------ */

static void calc_bbb (unsigned int sequence, char *bbb) {

	int		lsb;
	int		msb;

	sequence--;			/* Bias sequence to 0 */
	strcpy (bbb, "PZZ");		/* Provide default value for bbb (used for any seq > 675) */

	if (sequence < MAX_SEQ_NUM) {
		msb = sequence / BASE;
		lsb = sequence % BASE;

		sprintf (bbb, "P%c%c", msb + 'A', lsb + 'A');
	}
}

/* ------------------------------------------------------------------------------

Function Name:
	goesrCmiFile2Wmo

Synopsis:
	This function calculates a WMO header from a file name string.


Prototype:
	void goesrCmiFile2Wmo (char *goesrFileName, char *wmo_header)

Arguments:

	Name:		goesrFileName
	Type:		char *
	Access:		read only

	goesrFileName is the name of a GOES-R CMI product file as transferred
	from the GOES-R PD to AWIPS.

	Name:		wmo_header
	Type:		char *
	Access:		write only

	wmo_header is an output variable containing the WMO header of the form:
	  	TTAAII<sp>CCCC<sp>YYGGgg<sp>bbb

	where:

		<sp> is the ASCII space character

	The memory for this variable must be allocated by the caller.

Description:
	Generate a WMO header for a GOES-R Advanced Baseline Imager (ABI) Cloud
	and Moisture Imagery (CMI) product based on the file name as defined in
	the GOES-R/AWIPS ICD.  There are two formats: one for non-mesoscale
	products and one for mesoscale products.

	Non-mesoscale format:

		zy_xxxx-rrr-Bnn-MnCnn-Tnnn_Gnn_sYYYYDDDhhmmss_cYYYYDDDhhmmss.nc

		z = Environment
			I - Integrated Test Environment (ITE)
			D - Development Environment (DE)
			O - Operational Environment (OE)

		y = Data Type
			R - Real-time
			P - Playback
			S - Simulated
			T - Test

		xxxx = Product Region
			ECONUS - East CONUS
			WCONUS - West CONUS
			TCONUS - Center position CONUS
			HIREGI - Hawaii Regional
			PRREGI - Puerto Rico Regional
			AKREGI - Alaska Regional
			EFD    - East Full Disk
			WFD    - West Full Disk
			TFD    - Center position Full Disk

		rrr = Resolution
			005 - 0.5 km
			010 - 1.0 km
			025 - 2.5 km
			280 - 28 km

		Bnn = Bit Depth
			B08 - 8 bits
			B09 - 9 bits
			B10 - 10 bits
			B11 - 11 bits
			B12 - 12 bits
			B13 - 13 bits
			B14 - 14 bits

		Mn = ABI Mode
			M3 - ABI Mode 3
			M4 - ABI Mode 4

		Cnn - ABI Channel
			C01 - ABI Channel 1
			C02 - ABI Channel 2
			...
			C16 - ABI Channel 16

		Tnnn = Tile Number
			T001 - Tile 1 (upper left corner)
			...
			Txxx - Last tile (lower right corner) depends on product and tile size

		Gnn = Satellite Number
			G16 - GOES-R
			G17 - GOES-S
			...
			Gnn - Additional GOES-R-series satellites

		sYYYYDDDhhmmss = Start date/time of ABI scene
			YYYY - year
			DDD - Julian day of year
			hh - hour (00 - 23)
			mm - minute (00 - 59)
			ss - second (00 - 59)

		cYYYYDDDhhmmss = File creation time of first tile generated for product
			YYYY - year
			DDD - Julian day of year
			hh - hour (00 - 23)
			mm - minute (00 - 59)
			ss - second (00 - 59)


	Mesoscale format:

		zy_xxxx-rrr-Bnn-Sn-NxxWyyy-MnCnn-Tnnn_Gnn_sYYYYDDDhhmmss_cYYYYDDDhhmmss.nc

		z = Environment
			I - Integrated Test Environment (ITE)
			D - Development Environment (DE)
			O - Operational Environment (OE)

		y = Data Type
			R - Real-time
			P - Playback
			S - Simulated
			T - Test

		xxxx = Product Region
			EMeso  - East Satellite Mesoscale
			WMeso  - West Satellite Mesoscale
			TMeso  - Center Position Mesoscale

		rrr = Resolution
			005 - 0.5 km
			010 - 1.0 km
			025 - 2.5 km
			280 - 28 km

		Bnn = Bit Depth
			B08 - 8 bits
			B09 - 9 bits
			B10 - 10 bits
			B11 - 11 bits
			B12 - 12 bits
			B13 - 13 bits
			B14 - 14 bits

		Sn = Mesoscale scene indicator
			S1 - Mesoscale scene 1
			S2 - Mesoscale scene 2

		NxxWyyy - Lat/Long center point of mesoscale image
			Nxx -	Latitude value where N can be either 'N' (North) or 'S' (South)
				followed by two digit latitude value.
			Wyyy -	Longitude value where W can be either 'E' (East) or 'W' (West)
 				followed by three digit longitude value.

		Mn = ABI Mode
 			M3 - ABI Mode 3
			M4 - ABI Mode 4

		Cnn - ABI Channel
			C01 - ABI Channel 1
			C02 - ABI Channel 2
			...
			C16 - ABI Channel 16

		Tnnn = Tile Number
			T001 - Tile 1 (upper left corner)
			...
			Txxx - Last tile (lower right corner) depends on product and tile size

		Gnn = Satellite Number
			G16 - GOES-R
			G17 - GOES-S
			...
			Gnn - Additional GOES-R-series satellites

		sYYYYDDDhhmmss = Start date/time of ABI scene
			YYYY - year
			DDD - Julian day of year
			hh - hour (00 - 23)
			mm - minute (00 - 59)
			ss - second (00 - 59)

		cYYYYDDDhhmmss = File creation time of first tile generated for product
			YYYY - year
			DDD - Julian day of year
			hh - hour (00 - 23)
			mm - minute (00 - 59)
			ss - second (00 - 59)

Return Values:
	0	- Success
	1	- Failure - a valid WMO header was not generated.

------------------------------------------------------------------------------ */

int goesrCmiFile2Wmo (char *goesrFileName, char *wmo_header) {

//	char		environment;				/* Environment in which this product was created - I, D, or O */
//	char		data_type;				/* Product mode - 'R'eal-time, 'P'layback, 'S'imulated, or 'T'est */
	char		scene_name[MAX_SCENE_NAME_LEN+1];	/* Name of scene (e.g. - AKNatl) */
//	int		resolution;				/* Resolution of scene in hectometers (.1 km) */
//	int		bit_depth;				/* bits used per pixel */
	int		meso_scene;				/* Mesoscale scene number (1 or 2) */
	int		center_latitude;			/* Center point latitude */
	int		center_longitude;			/* Center point longitude */
	int		lat_dir;				/* latitude direction */
	int		long_dir;				/* longitude direction */
	int		abi_mode;				/* ABI instrument mode for this scene */
	int		channel;				/* Channel for this scene */
	int		tile_num;				/* Tile number */
	int		sat_num;				/* Satellite number */
	char		scene_time[DATE_STR_LEN+1];		/* Scene time from file name */
	char		wmo_time[WMO_DATE_LEN+1];		/* Date string as used in WMO header */
	int		i;					/* array index/loop counter */
	char		field[FIELD_COUNT][MAX_FIELD_LEN+1];	/* array of character strings for individual tokens */
	char		*str;					/* character pointer used for parsing */
	char		*token;					/* temporary string pointer */
	char		isMeso;					/* set TRUE for mesos, FALSE for non-mesos */
	char		buf[4];					/* temporary buffer for lat/long calculations */
	char		a2;					/* A2 field of WMO header */
	int		ii;					/* ii field of WMO header */
	char		bbb[SIZE_BBB+1];			/* optional bbb field of WMO header */
	char		fname[MAX_STR_LEN+1];
	struct tm	tms;

	wmo_header[0] = '\0';

	strncpy (fname, goesrFileName, MAX_STR_LEN);

	if (strlen (fname) == 0) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - File name is null or zero length", __FUNCTION__);
		return 1;
	}

//	printf ("File name: %s in goesr_cmiFile2wmo\n", fname);

	/* Break the file name down into tokens separated by '_' and put into 'field' array */
	for (i = 0, str = fname; i < FIELD_COUNT - 1; i++, str = NULL) {
		token = strtok (str, FIELD_SEPARATOR);

		if (token == NULL) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - strtok unexpectedly returned NULL", __FUNCTION__);
			return 1;
		}

		if (strlen (token) > MAX_FIELD_LEN) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Token length (%d) > MAX_FIELD_LEN (%d)",
				__FUNCTION__, (int) strlen (token), MAX_FIELD_LEN);
			return 1;
		}
		strcpy (field[i], token);

//		printf ("field[%d] = %s\n", i, field[i]);
	}

	if (i != (FIELD_COUNT - 1)) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - token count (%d) != %d in goesr_cmiFile2wmo",
			__FUNCTION__, i, FIELD_COUNT - 1);
		return 1;
	}

	/* Strip the file extension from the last token */
	token = strtok (NULL, ".");
	strcpy (field[i], token);
//	printf ("field[%d] = %s\n", i, field[i]);

	if (strlen (field[0]) != 2) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - file name field 1 \"%s\" is an invalid length (%d != 2)",
			__FUNCTION__, field[0], (int) strlen (field[0]));
		return 1;
	}

//	environment = field[0][0];
//	data_type = field[0][1];

//	printf ("Environment = %c, Data Type = %c\n", environment, data_type);

	if ((token = strtok (field[1], SUBFIELD_SEPARATOR)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - could not strtok field[1] subfield 0 (%s)",
			__FUNCTION__, field[1]);
		return 1;
	}

	if (strlen (token) > MAX_SCENE_NAME_LEN) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - scene name length (%d) too long: \"%s\"",
			__FUNCTION__, (int) strlen (token), token);
		return 1;
	}

	strcpy (scene_name, token);
	raiseCase (scene_name);
//	printf ("Scene name: %s\n", scene_name);

	if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - could not strtok field[1] subfield 1 (%s)",
			__FUNCTION__, field[1]);
		return 1;
	}

	if (strlen (token) != SIZE_RESOLUTION) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - resolution field incorrect size (%d), should be %d",
			__FUNCTION__, (int) strlen (token), SIZE_RESOLUTION);
		return 1;
	}

	if (!isNumber (token)) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - resolution is not numeric (%s)",
			__FUNCTION__, token);
		return 1;
	}

//	resolution = atoi (token);
//	printf ("Resolution = %d\n", resolution);

	if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - could not strtok field[1] subfield 2 (%s)",
			__FUNCTION__, field[1]);
		return 1;
	}

	if (strlen (token) != SIZE_BITDEPTH) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - bit depth field incorrect size (%d), should be %d",
			__FUNCTION__, (int) strlen (token), SIZE_BITDEPTH);
		return 1;
	}

	if (!isNumber (&token[1])) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - bit depth is not numeric (%s)",
			__FUNCTION__, token);
		return 1;
	}

//	bit_depth = atoi (&token[1]);
//	printf ("Bit depth = %d\n", bit_depth);

	if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - could not strtok field[1] subfield 3 (%s)", __FUNCTION__, field[1]);
		return 1;
	}

	switch (token[0]) {
		case 'S':
			isMeso = TRUE;
			break;
		case 'M':
			isMeso = FALSE;
			break;
		default:
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - unexpected value of field 1 subfield 3 (%c)",
				__FUNCTION__, token[0]);
			return 1;
			break;
	}

	if (isMeso) {	// zy_xxxx-rrr-Bnn-Sn-NxxWyyy-MnCnn-Tnnn_Gnn_sYYYYDDDhhmmss_cYYYYDDDhhmmss.nc
//		printf ("This is a mesoscale product\n");
		meso_scene = atoi (&token[1]);

		if ((meso_scene != 1) && (meso_scene != 2)) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Unexpected value of mesoscale scene %d",
				__FUNCTION__, meso_scene);
			return 1;
		}

//		printf ("Mesoscale scene = %d\n", meso_scene);

		if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Could not strtok field[1] subfield 3 (%s)",
				__FUNCTION__, field[1]);
			return 1;
		}

		if (strlen (token) != SIZE_LOCATION) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Incorrect size (%d) of Lat/Long field, should be %d",
				__FUNCTION__, (int) strlen (token), SIZE_LOCATION);
			return 1;
		}

		if ((token[0] != 'N') && (token[0] != 'S')) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Unknown value %c for latitude direction",
				__FUNCTION__, token[0]);
			return 1;
		}

		if ((token[3] != 'E') && (token[3] != 'W')) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Unknown value %c for longitude direction",
				__FUNCTION__, token[3]);
			return 1;
		}

		lat_dir = (token[0] == 'N') ? 1 : -1;
		long_dir = (token[3] == 'E') ? 1 : -1;

		memset (buf, '\0', 4);
		memcpy (buf, &token[1], SIZE_LAT);
		center_latitude = lat_dir * atoi (buf);

		memset (buf, '\0', 4);
		memcpy (buf, &token[4], SIZE_LONG);
		center_longitude = long_dir * atoi (buf);

		center_longitude = center_longitude == 180 ? -180 : center_longitude;

//		printf ("Center point = %d lat/%d long\n", center_latitude, center_longitude);

		a2 = findGoesrCmiMesoRegionID (center_latitude, center_longitude);

		if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
			logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Could not strtok mode/channel subfield",
				__FUNCTION__);
			return 1;
		}

	} else {	// Non-mesoscale product - zy_xxxx-rrr-Bnn-MnCnn-Tnnn_Gnn_sYYYYDDDhhmmss_cYYYYDDDhhmmss.nc
//		printf ("This is a non-mesoscale product\n");
		a2 = findGoesrCmiNonMesoRegionID (scene_name);
	}

	if (token[0] != 'M') {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Unknown value for ABI field %c, should be 'M'",
			__FUNCTION__, token[0]);
		return 1;
	}

	abi_mode = token[1] - '0';

	if ((abi_mode != 3) && (abi_mode != 4)) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid ABI mode %d",
			__FUNCTION__, abi_mode);
		return 1;
	}

//	printf ("ABI mode = %d\n", abi_mode);

	if (token[2] != 'C') {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Unknown value for channel field ID %c, should be 'C'",
			__FUNCTION__, token[2]);
		return 1;
	}

	channel = atoi (&token[3]);

	if ((channel < 1) || (channel > 16)) {
		logMsg (eLog, V_ERROR, S_ERROR, "Invalid ABI mode %d\n", abi_mode);
		return 1;
	}

//	printf ("Channel = %d\n", channel);

	if ((token = strtok (NULL, SUBFIELD_SEPARATOR)) == NULL) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Could not strtok tile number subfield",
			__FUNCTION__);
		return 1;
	}

	if (strlen (token) != SIZE_TILE + 1) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Incorrect size (%d) of tile number field, should be %d",
			__FUNCTION__, (int) strlen (token), SIZE_TILE+1);
		return 1;
	}

	if (token[0] != 'T') {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid tile number specifier '%c'",
			__FUNCTION__, token[0]);
		return 1;
	}

	tile_num = atoi (&token[1]);
	if ((tile_num < 1) || (tile_num > 999)) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid tile number %d",
			__FUNCTION__, tile_num);
		return 1;
	}

//	printf ("Tile number = %d\n", tile_num);

	if (field[2][0] != 'G') {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid satellite number specifier '%c'",
			__FUNCTION__, field[2][0]);
		return 1;
	}

	sat_num = atoi (&field[2][1]);

	if (sat_num < 16) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid satellite number %d",
			__FUNCTION__, sat_num);
		return 1;
	}

//	printf ("Satellite number = %d\n", sat_num);

	strcpy (scene_time, &field[3][1]);
	if (strlen (scene_time) != DATE_STR_LEN) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid date string length %d, should be %d",
			__FUNCTION__, (int) strlen (scene_time), DATE_STR_LEN);
		return 1;
	}

//	printf ("Scene time = %s\n", scene_time);

	strptime (scene_time, "%Y%j%H%M%S", &tms);
	strftime (wmo_time, WMO_DATE_LEN+1, "%d%H%M", &tms);

//	if (!formatDateString ("%Y%j%H%M%S", "%d%H%M", 0, scene_time, wmo_time, WMO_DATE_LEN+1)) {
//		logMsg (eLog, V_ERROR, S_ERROR, "Could not parse scene time\n");
//		return;
//	}

//	printf ("WMO time = %s\n", wmo_time);

	if (!isalpha(a2)) {
		logMsg (eLog, V_ERROR, S_ERROR, "(%s) - Invalid a2 character '%c'",
			__FUNCTION__, a2);
		return 1;
	}

	ii = calc_ii (channel);

	calc_bbb (tile_num, bbb);
	sprintf (wmo_header, "%c%c%c%c%02d %s %s %s",
			WMO_T1_SATELLITE,
			WMO_T2_IMAGERY,
			isMeso ? WMO_A1_GOESR_CMI_MESO : WMO_A1_GOESR_CMI_NON_MESO,
			a2,
			ii,
			WMO_ORIGINATOR,
			wmo_time,
			bbb);

	// printf ("WMO header: %s\n", wmo_header);
	logMsg (eLog, V_DEBUG, S_DEBUG, "(%s) - Generated WMO: %s", __FUNCTION__, wmo_header);
	return 0;
}


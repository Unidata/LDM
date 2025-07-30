/*
 * goesr_lib.h
 *
 *  Created on: Nov 17, 2013
 *      Author: brapp
 */

#ifndef GOESR_LIB_H_
#define GOESR_LIB_H_

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

int goesrCmiFile2Wmo (char *goesrFileName, char *wmo_header);

#endif /* GOESR_LIB_H_ */

#ifndef _WMO_HEADER_
#define _WMO_HEADER_
typedef struct {
        int min;        
        int hour;       
        int mday;       
        int mon;        
        int year;       
} dtime;


typedef struct {
        char TT[3];     /* Tsub1Tsub2 : Data type and/or form */
        char AA[3];     /* Asub1Asub2 : Geograph. and/or time */
        int ii;
        char CCCC[5];  /* station of origin or compilation */
        char PIL[10];
        dtime time;
	char DDHHMM[7];
        char BBB[10]; 
	char model[256];
} wmo_header_t;

#endif

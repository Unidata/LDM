#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <sys/time.h>
  
#define HASH_TABLE_SIZE 1000

#define PORT                        9127
#define MAXLINE                     1024
#define SAMPLE_SIZE                 5
#define SBN_FRAME_SIZE              4000
#define MIN_SOCK_TIMEOUT_MICROSEC   9000

const char* const COPYRIGHT_NOTICE  = "Copyright (C) 2021 "
            "University Corporation for Atmospheric Research";
const char* const PACKAGE_VERSION   = "0.0.1";

typedef struct sockaddr_in SOCK4ADDR;


typedef struct Frame {
    uint32_t        seqNum;
    unsigned char*  frameData;    
} Frame_t;


// A hashtable of numbers
Frame_t frameHashTableRun1[HASH_TABLE_SIZE];
Frame_t frameHashTableRun2[HASH_TABLE_SIZE];

/**
 * Unconditionally logs a usage message.
 *
 * @param[in] progName   Name of the program.
 * @param[in] copyright  Copyright notice.
 */
static void usage(
    const char* const          progName,
    const char* const restrict copyright)
{
    /*
    int level = log_get_level();
    (void)log_set_level(LOG_LEVEL_NOTICE);

    log_notice_q(
    */
    printf(
"\n\t%s - version %s\n"
"\n\t%s\n"
"\n"
"Usage: %s [v|x] [-l log] [-m addr] [-I ip_addr] [-R bufSize]\n"
"where:\n"
"   -I ip_addr  Listen for multicast packets on interface \"ip_addr\".\n"
"               Default is system's default multicast interface.\n"
"   -l dest     Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `dest`. Default is \"%s\"\n"
"   -m addr     Read data from IPv4 dotted-quad multicast address \"addr\".\n"
"               Default is to read from the standard input stream.\n"
"   -R bufSize  Receiver buffer size in bytes. Default is system dependent.\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"\n",
        progName, PACKAGE_VERSION, copyright, progName);

//    (void)log_set_level(level);

    exit(0);
}


void
joinMulticastGroup(struct in_addr mcastAddr, char* imr_interface, int sockfd) {

    int                 rc;

    SOCK4ADDR  cliAddr, servAddr = {};
    struct ip_mreq      mreq; 
    struct hostent*     h;

    /* Join multicast group */
    mreq.imr_multiaddr.s_addr = mcastAddr.s_addr;
    mreq.imr_interface.s_addr = (imr_interface == NULL )
        ? htonl(INADDR_ANY)
        : inet_addr(imr_interface);
    
    // mreq.imr_interface.s_addr = inet_addr("192.168.0.83");
    // Use my IP address: "97.122.92.142"
    // mreq.imr_interface.s_addr = inet_addr("97.122.92.142"); 
    mreq.imr_interface.s_addr = htonl(INADDR_ANY); 

    rc = setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) &mreq,
                  sizeof(mreq));

    if (rc < 0) {
        //log_error_q("cannot join multicast group '%s'",
        printf("cannot join multicast group '%s'",
                inet_ntoa(mcastAddr));
        exit(1);
    }
}

/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.

 * @param[out] mcastSpec      Specification of multicast group.
 * @param[out] interface      Specification of interface on which to listen.
 * @param[out] rcvBufSize     Receiver buffer size in bytes
 * @retval     0              Success.
 * @retval     EINVAL         Error. `log_add()` called.
 */
static int
decodeCommandLine(
        int                    argc,
        char**  const restrict argv,
        char**  const restrict mcastSpec,
        char**  const restrict imr_interface,
        int*    const restrict sockTimeOut,
        int*    const restrict rcvBufSize)
{
    int                 status = 0;
    extern int          optind;
    extern int          opterr;
    extern char         *optarg;
    extern int          optopt;
    
    int                 ch;
    
    opterr = 0;                         /* no error messages from getopt(3) */
    /* Initialize the logger. */
/*    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
*/
    while (0 == status &&
           (ch = getopt(argc, argv, "vxI:l:m:R:r:")) != -1) {
        switch (ch) {
            case 'v':
                    printf("set verbose mode");
                break;
            case 'x':
                    printf("set debug mode");
                break;
            case 'I':
                    *imr_interface = optarg;
                break;
            case 'l':
                    printf("logger spec");
                break;
            case 'm':
                    *mcastSpec = optarg;
                break;
            case 'R':
                if (sscanf(optarg, "%lf", rcvBufSize) != 1 || *rcvBufSize <= 0) {
                       printf("Invalid receive buffer size: \"%s\"", optarg);
                       //log_add("Invalid receive buffer size: \"%s\"", optarg);
                       status = EINVAL;
                }
                break;
            case 'r':
                if (sscanf(optarg, "%d", sockTimeOut) != 1 || *sockTimeOut < 0) {
                       printf("Invalid socket time-out value: \"%s\"", optarg);
                }
                break;
            default:
                break;        
        }
    }

    if (argc - optind != 0)
        usage(argv[0], COPYRIGHT_NOTICE);

    return status;
}

// hash table is empty
void initHashTable(Frame_t *frameHashTable)
{
    for (int i=0; i< HASH_TABLE_SIZE; i++)
    {
      frameHashTable[i].seqNum = -1;
      frameHashTable[i].frameData = NULL;
    }
}



// key is the sequenceNumber
int hashMe(uint32_t seqNumKey)
{
    int hash_value = 0;

    hash_value = seqNumKey % HASH_TABLE_SIZE; 

    return hash_value;
}


void printHashTable(Frame_t *frameHashTable)
{
    for (int i=0; i< HASH_TABLE_SIZE; i++)
    {
      if(frameHashTable[i].seqNum == -1)
      {
        //printf("%d - %s\n", i, "---");  
      }
      else
      {
        printf("\t%d --> Frame(%lu, %lu)\n", i, frameHashTable[i].seqNum, frameHashTable[i].frameData);
      }
    }
}

void removeFrameFromHashTable(  Frame_t *frameHashTable, 
                                uint32_t sequenceNumber)
{

    int index = hashMe(sequenceNumber);
    frameHashTable[index].seqNum = -1;
    frameHashTable[index].frameData = NULL;
}

static
int retrieveHeaderFields(   unsigned char   *buffer, 
                            uint32_t        *pSequenceNumber, 
                            uint16_t        *pRun, 
                            uint16_t        *pCheckSum)
{
    int status = 0;     // success

    // receiving: SBN 'sequence': [8-11]
    *pSequenceNumber = (uint32_t) ntohl(*(uint32_t*)(buffer+8)); 

    // receiving SBN 'run': [12-13]
    *pRun = (uint16_t) ntohs(*(uint16_t*) (buffer+12));   

    // receiving SBN 'checksum': [14-15]
    *pCheckSum =  (uint16_t) ntohs(*(uint16_t*) (buffer+14));  

    // Compute SBN checksum on 2 bytes as an unsigned sum of bytes 0 to 13
    uint16_t sum = 0;
    for (int byteIndex = 0; byteIndex<14; byteIndex++)
    {
        sum += (unsigned char) buffer[byteIndex];
    }

    if( *pCheckSum != sum) 
    {
        status = -1;
    }

    return status;
}

static int
getWellFormedFrame(unsigned char *buffer, int n)
{

    int status = -1;     // failure

    int byteIndex;
    for (byteIndex = 0; byteIndex < n; byteIndex++)
    {                
        // NOTE!    Beginning of frame comes at poistion byteIndex when byte == 255
        if (buffer[byteIndex] == 255) 
        {   
            // Frame found at position: byteIndex
            //printf("\t\t===> Server: %d-th frame received starts at byte: %d \n", n, byteIndex);             
            buffer[n] =  '\0';

            return byteIndex;
        }
    }

    // no SBN frame header detected in current received buffer 
    return status;

}


bool insertFrameIntoHashTable(int sessionFlag, uint32_t sequenceNumber, unsigned char* buffer,
                            int *pNumberOfFramesReceivedRun1, int *pNumberOfFramesReceivedRun2 )
{

    int numberOfFramesReceivedRun1 = *pNumberOfFramesReceivedRun1;
    int numberOfFramesReceivedRun2 = *pNumberOfFramesReceivedRun2;

    printf("\n\tSequence# : %d - inserted!\n", sequenceNumber);
    int index = hashMe(sequenceNumber);
    if(sessionFlag == 1) 
    {

        if( frameHashTableRun1[index].seqNum != -1)
        {
            printf("Sequence# : %u - Collision in buffer #1\n", sequenceNumber);
            return false;   // collision
        }

        // insertIt
        frameHashTableRun1[index].seqNum       = sequenceNumber;
        frameHashTableRun1[index].frameData    = buffer;
        numberOfFramesReceivedRun1++;
        
    }
    else
    {     
        
        if( frameHashTableRun2[index].seqNum != -1)
        {
            printf("Sequence# : %u - Collision in buffer #2\n", sequenceNumber);
            return false;   // collision
        }
        // insertIt
        frameHashTableRun2[index].seqNum       = sequenceNumber;
        frameHashTableRun2[index].frameData    = buffer;
        numberOfFramesReceivedRun2++;
        
    }
    *pNumberOfFramesReceivedRun1 = numberOfFramesReceivedRun1;
    *pNumberOfFramesReceivedRun2 = numberOfFramesReceivedRun2;
    return true;
}

int 
sendFrame(unsigned char *data)
{
    int status = 0;
    //printf("Sending frame: %s\n", data);
    // send this frame to the noaaportIngester listening on a multicast group IP address.
    return status;
}

int
sendTopFrameToIngester( int         sessionFlip,
                        uint32_t    *pLastSequenceNumberSentRun1,
                        uint32_t    *pLastSequenceNumberSentRun2,
                        int         *pNumberOfFramesReceivedRun1,
                        int         *pNumberOfFramesReceivedRun2
                    )
{
    int         status = 0; // success
    
    uint32_t    *pLastSequenceNumberSent = NULL;
    uint32_t    lastSequenceNumberSent = 0;
    Frame_t     *frameHashTable;
    bool        otherBuffer = false;

    if(sessionFlip == 1)
    {
        frameHashTable              = frameHashTableRun1;
        lastSequenceNumberSent      = *pLastSequenceNumberSentRun1;

        // Are there still frames left over in other buffer?
        if( *pNumberOfFramesReceivedRun2 > 0 )
        {   
            otherBuffer = true;
            frameHashTable          = frameHashTableRun2;   
            lastSequenceNumberSent = *pLastSequenceNumberSentRun2;
        }
        else
        {
            // No more frames from other buffer: reset both buffer's last sequence number to zero
            *pLastSequenceNumberSentRun2 = 0;   
        }
    }
    else
    {

        frameHashTable              = frameHashTableRun2;
        lastSequenceNumberSent     = *pLastSequenceNumberSentRun2;

        // Are there still frames left over in other buffer?
        if( *pNumberOfFramesReceivedRun1 > 0 )    
        {
            otherBuffer = true;
            frameHashTable          = frameHashTableRun1;   
            lastSequenceNumberSent  = *pLastSequenceNumberSentRun1;
        }
        else
        {
            // No more frames from other buffer: reset both last sequence numbers to zero
            *pLastSequenceNumberSentRun1 = 0;   
        }
    }

   
    for (int i=0; i< HASH_TABLE_SIZE; i++)
    {
        if(frameHashTable[i].seqNum == -1 )
            continue;

        printf("\n\tAbout to send frame #%d with SeqNum: %u\n", 
            i, frameHashTable[i].seqNum);
        
        // Do not resend the same frame if a duplicate frame arrives but does not get
        // rejected from hash table because it has already been purged from the table 
        // (original frame was already sent and its location already reset)
        if( lastSequenceNumberSent < frameHashTable[i].seqNum )
        {
            int status = sendFrame(frameHashTable[i].frameData);

            printf("\tFrame #%d sent correctly! \n", i);

            // Update lastSequenceNumberSent AND the number of remaining frames not sent yet
            if( (sessionFlip == 1 && !otherBuffer) || ( sessionFlip == 2 && otherBuffer) )
            {    
                *pLastSequenceNumberSentRun1 = frameHashTable[i].seqNum;
                (*pNumberOfFramesReceivedRun1)--;
            }
            else
            {
                if ( (sessionFlip == 1 && otherBuffer) || ( sessionFlip == 2 && !otherBuffer) )
                {
                    *pLastSequenceNumberSentRun2 = frameHashTable[i].seqNum;
                    (*pNumberOfFramesReceivedRun2)--;
                    
                }
            }

            //Reset frame's location in table...
            frameHashTable[i].seqNum    = -1;
            frameHashTable[i].frameData = NULL;

            return status;
        }
        else 
        {
            printf("\n\t sendTopFrameToIngester(): FRAME NOT sent (cause it's NOT oldest) !!!!--> %d-th frame: lastSequenceNumberSent: %d - seqNum to send: %d\n\n\n", 
                i, lastSequenceNumberSent, frameHashTable[i].seqNum);
        }

    }

    status = -1;
    printf("sendTopFrameToIngester(): No frame to send (empty queue)...\n");
    return status;
}

void printCounters(char *stage, int numberOfFramesReceivedRun1, 
                                int numberOfFramesReceivedRun2,
                                uint32_t lastSequenceNumberSentRun1,
                                uint32_t lastSequenceNumberSentRun2)

{
    printf("\n%s counters:\n", stage);   
    printf("\tNumber of frames received (so far): \tSession1: %d,  Session2: %d \n", 
        numberOfFramesReceivedRun1, numberOfFramesReceivedRun2);
    printf("\tLast Sequence Number (sent):\t\tSession1: %d,  Session2: %d \n\n", 
        lastSequenceNumberSentRun1, lastSequenceNumberSentRun2);
}

void setTimerOnSocket(int *pSockFd, int microSec)
{
    // set a timeout on the receiving socket
    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = microSec;
    setsockopt(*pSockFd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

}
/********************************************************/

static int 
execute(char* mcastSpec, char* imr_interface, int sockTimeOut, int rcvBufSize) {

    int status = 0;

    int                 sd, rc, n, len;
    socklen_t           cliLen;
    
    struct in_addr      mcastAddr;
    
    SOCK4ADDR servaddr= { .sin_family       = AF_INET, 
                          .sin_addr.s_addr  = htonl(INADDR_LOOPBACK), 
                          .sin_port         = htons(PORT)
                        }, 
                        cliaddr;
    
    int sockfd;
    unsigned char buffer[SBN_FRAME_SIZE] = {};
    
    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);

    }

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) )
    {
        perror("bind failure!");
        exit(EXIT_FAILURE);
    }


    //joinMulticastGroup(mcastAddr, imr_interface, sockfd);
    
    // set a timeout on the receiving socket, sockTimeOut in micro seconds
    setTimerOnSocket(&sockfd, sockTimeOut);
    
    // Initialize the SBN hash table
    initHashTable(frameHashTableRun1);
    initHashTable(frameHashTableRun2);

    Frame_t *frameHashTable;    
    
    int         totalFramesReceived             = 0;
    int         numberOfFramesReceivedRun1      = 0;
    int         numberOfFramesReceivedRun2      = 0;
    int         maxFramesToKeep                 = 6;    // max is 1000 (default), or input from user

    uint16_t    previousRun                     = 0;
    uint16_t    currentRun                      = 0;
    int         sessionRun                      = 1;
    bool        sessionFlipped                  = true;
    
    
    uint16_t    checkSum;
    uint32_t    sequenceNumber;
    uint32_t    lastSequenceNumberSentRun1      = 0;
    uint32_t    lastSequenceNumberSentRun2      = 0;


    /* Infinite server loop */  
    for (;;) 
    {


        // Start measuring time
        struct timeval debut, fin;
        gettimeofday(&debut, 0);
        

        cliLen = sizeof(cliaddr); 

        n = recvfrom(sockfd, (char *)buffer, MAXLINE,
                    MSG_WAITALL, ( struct sockaddr *) &cliaddr,
                    &cliLen);

        if( n < 0) 
        {
            if (errno == EWOULDBLOCK) 
            {
                // send remaining frames (purge current buffer)
                if( numberOfFramesReceivedRun1 > 0 || numberOfFramesReceivedRun2 > 0 )
                {
                    printf("\n===> Server: Wake up and send remaining frames (purge current buffer)..\n");

                    if( sendTopFrameToIngester(sessionRun, 
                            &lastSequenceNumberSentRun1, &lastSequenceNumberSentRun2, 
                            &numberOfFramesReceivedRun1, &numberOfFramesReceivedRun2)  == -1)
                    {
                        printf("Error sending frame...\n" );
                    }
                }  
            } else 
            {
                perror("recvfrom error");
            }

            continue;
        }



        if (n > 0 )
        { 
            // globally, i.e. for both hashTable
            totalFramesReceived++;
            printf("\n\t =============== Total frames received so far: %d =================\n\n", totalFramesReceived);

            // Calculate the elapsed time between 2 recvfrom() calls
            gettimeofday(&fin, 0);
            long seconds = fin.tv_sec - debut.tv_sec;
            long microseconds = fin.tv_usec - debut.tv_usec;
            double elapsed = seconds + microseconds*1e-6;
            printf("\n\t UDP packet receiving rate: %lf\n\n", elapsed);
        }

        int byteIndex;
        if( (byteIndex = getWellFormedFrame(buffer, n) ) < 0)
        {
           
            printf("\n\t =============== No new frame detected in buffer =================\n\n");
            continue;
        }
        
        // TO-DO: take into account that the byte:255 may NOT be be in poll position, but offet by byteIndex!
        int offset = byteIndex; // First byte at 'byteIndex' position is 255: (buffer size is sizeof(int))

        if( retrieveHeaderFields(buffer, &sequenceNumber, &currentRun, &checkSum) != 0) 
        {
            printf("\n\t =============== Checksum failed =================\n\n");
            continue;   // checksum failed
        } 


        // SBN run number
        // Determine when the session has flipped: assumption: we can only have one session running at any one time
        // if 'previousRun' has a non-zero value (i.e. not at start)
        //  and run# has changed from previous
        // Consequently, the new sequence number has to be reset from the sender side (client)
        if( previousRun && (previousRun != currentRun ))
        {
            sessionRun = sessionRun == 1? 2:1;
            previousRun = currentRun;
            sessionFlipped = !sessionFlipped;            
        }

        if(sessionFlipped)
            printf("  Session NOT flipped: %d (current run: %d, previousRun: %d)\n", 
                sessionRun, currentRun, previousRun);
        else
            printf("  Session HAS flipped: %d (current run: %d, previousRun: %d)\n", 
                sessionRun, currentRun, previousRun);

        // initializing previousRun at start of very first session
        if( !previousRun )
        {
            previousRun = currentRun;
        }
        

        if ( !insertFrameIntoHashTable( sessionRun, sequenceNumber, buffer, 
                    &numberOfFramesReceivedRun1, &numberOfFramesReceivedRun2))
        {
            printf("Collision occurred!"); // in case of a collision, numberOfFramesReceived is NOT incremented!
            continue;
        }          
        
        int mod6 = (numberOfFramesReceivedRun2 + numberOfFramesReceivedRun1) % maxFramesToKeep;
        printf("\t%d + %d mod %d = %d \n", numberOfFramesReceivedRun1, numberOfFramesReceivedRun2, maxFramesToKeep, mod6);
        
        if ( mod6 == 0 )  
        {            
            printCounters("Current", numberOfFramesReceivedRun1, numberOfFramesReceivedRun2, 
                            lastSequenceNumberSentRun1, lastSequenceNumberSentRun2);

            if( sendTopFrameToIngester(sessionRun, 
                    &lastSequenceNumberSentRun1, &lastSequenceNumberSentRun2, 
                    &numberOfFramesReceivedRun1, &numberOfFramesReceivedRun2)  == -1)

                printf("No frame sent!\n");
        }  

        printCounters("New", numberOfFramesReceivedRun1, numberOfFramesReceivedRun2, 
                        lastSequenceNumberSentRun1, lastSequenceNumberSentRun2);
                    

        // show partially populated hash table
        if(numberOfFramesReceivedRun1 > 0)
        {
            printf("\n========== Show Hash Table for Run=1 (thus far): ================================\n");
            printHashTable(frameHashTableRun1);
            printf("=================================================================================\n");
        }
        if(numberOfFramesReceivedRun2 > 0)
        {
            printf("\n========== Show Hash Table for Run=2 (thus far): ================================\n");
            printHashTable(frameHashTableRun2);
            printf("=================================================================================\n");
        }


        printf("Continue receiving..\n\n");
        // push message to queue via the hash table        

    } // for()

    return status;    
}



int main(
    const int argc,           /**< [in] Number of arguments */
    char*     argv[])         /**< [in] Arguments */
{
    int status;

    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);
/*    
    if (log_init(progname)) {
        log_syserr("Couldn't initialize logging module");
        status = -1;
    }
    else {
        (void)log_set_level(LOG_LEVEL_WARNING);

*/          
            char*   mcastSpec = NULL;
            char*   interface = NULL; // Listen on all interfaces unless specified on argv
            int     rcvBufSize= 0;
            int     socketTimeOut = MIN_SOCK_TIMEOUT_MICROSEC;
           
            status = decodeCommandLine(argc, argv, &mcastSpec, &interface, &socketTimeOut, &rcvBufSize); 

            if (status) {
                printf("Couldn't decode command-line");
/*              log_add("Couldn't decode command-line");
                log_flush_fatal();
*/                
                usage(progname, COPYRIGHT_NOTICE);
            }
            else {

                printf("\n\tStarting Up (v%s)\n", PACKAGE_VERSION);
                printf("\n\t%s\n", COPYRIGHT_NOTICE);
/*              log_notice("Starting up %s", PACKAGE_VERSION);
                log_notice("%s", COPYRIGHT_NOTICE);
*/
                status = execute(mcastSpec, interface, socketTimeOut, rcvBufSize);

                if (status) {
                    printf("Couldn't ingest NOAAPort data");
/*                  log_add("Couldn't ingest NOAAPort data");
                    log_flush_error();
*/                }
            }   /* command line decoded */
        
        //log_fini();
    

    return status ? 1 : 0;
}



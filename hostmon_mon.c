/*
     hostmon_mon.c     R. Hughes-Jones  The University of Manchester

     Aim is to send a stream of TCP messages 
     Use TCP socket to:
	   send a series (-l) of n byte (-p) "messages" to remote node with a specified interpacket interval (-w)
     Print local stats

*/

/*
   Copyright (c) 2020-2025 Richard Hughes-Jones, University of Manchester
   All rights reserved.

   Redistribution and use in source and binary forms, with or
   without modification, are permitted provided that the following
   conditions are met:

     o Redistributions of source code must retain the above
       copyright notice, this list of conditions and the following
       disclaimer. 
     o Redistributions in binary form must reproduce the above
       copyright notice, this list of conditions and the following
       disclaimer in the documentation and/or other materials
       provided with the distribution. 

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/


#define INSTANTIATE true


#include "net_test.h"                            /* common inlcude file */
#include "version.h"                             /* common inlcude file */

      
#define ERROR_MSG_SIZE 256
    char error_msg[ERROR_MSG_SIZE];                         /* buffer for error messages */

/* for command line options */
    extern char *optarg;
	
#define OUTFILENAME_MAXLEN 128

/* parameters */
    char out_filename[OUTFILENAME_MAXLEN];          /* name of file to open */ 
    int output_file =0;                          /* set =1 to write stats to file else to terminal */
    int interval_stats_sec =0;      	        /* time interval between reading TCP stats */ 
    int quiet = 0;                              /* set =1 for just printout of results - monitor mode */
    int verbose =0;                  		    /* set to 1 for printout (-v) */
    int extended_output =1;                     /* set to 1 for more printout (CPUStats */
    int cpu_affinity_core =-1;                  /* cpu affinity core -1 = core number not used this time */
    cpu_set_t cpu_affinity_cpuset;              /* cpu affinity from -a */
	int affinity_cpuset_inuse =0;               /* cpu_affinity_cpuset defined */


/* control */
    FILE *out_file = NULL;

/* timing */
    struct timeval before;           	        /* time before measurements */
    struct timeval after;            	        /* time after measurements */
    struct timeval now;
	StopWatch stopwatch_elapsed;                // data transfer time & elapsed time through the transfer
    double delta_t_elapsed = -1;
    double delta_t_elapsed_last = 0;

/* statistics */

    NET_SNMPStat net_snmp_stats;
    NETIFinfo net_if_info[NET_SNMP_MAX_IF];
    SNMPinfo snmp_info;


/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alrm(int signo);
static void cntlc_handler(int signo);
static void cntlz_handler(int signo);


int main (int argc, char **argv)
/* --------------------------------------------------------------------- */
{

/* statistics */

	
/* timing */
    long delay;                      		    /* time for one message loop */
    int wait_time_int=0;                        /* wait time to sleep */
/* timers */

    struct itimerval timer_value;               /* for the interval timer */
    struct itimerval timer_old_value;  

/* local variables */
    int ret;

/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */

/* set the signal handler for SIGALRM */
    signal (SIGALRM, sig_alrm);
/* define signal handler for cntl_c */
    signal(SIGINT, cntlc_handler);
/* define signal handler for cntl_z */
    signal(SIGTSTP, cntlz_handler);


/* get the input parameters */
    parse_command_line ( argc, argv);

/* set the CPU affinity of this process*/
	if(affinity_cpuset_inuse != 0){
		set_cpu_affinity_cpuset (&cpu_affinity_cpuset, quiet);
	}else {
		set_cpu_affinity_num (cpu_affinity_core, quiet);  // cover setting and not set cases
	}

/* initalise and calibrate the time measurement system */
    ret = RealTime_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);
    ret = StopWatch_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);

/* test system timer */	
    gettimeofday(&before, NULL);
    sleep(1);	
    gettimeofday(&after, NULL);
    delay = ((after.tv_sec - before.tv_sec) * 1000000) + (after.tv_usec - before.tv_usec);
    if(!quiet) printf("clock ticks for 1 sec = %ld us\n", delay);

/* open the output file for the stats - keep name constant for grapher */
    if(output_file){
		printf("Stats file name %s \n", out_filename);
		if((out_file = fopen(out_filename, "w") ) == NULL) {		    
			perror("Error: open of statistics file failed :");   
			exit(-1);
		}
    }
    else{
		/* use std output */
		out_file = stdout;
    }
		 
/* clear the local stats */

/* set the alarm to determine when to read the TCP stats.
   If a time is set for the length of the test the sig_alrm() handler sets loop_max to 0 to stop */
	if(verbose)printf("interval_stats_sec %d\n", interval_stats_sec);
	if(interval_stats_sec >0) {
		alarm(interval_stats_sec);
		timer_value.it_interval.tv_sec = interval_stats_sec;         /* Value to reset the timer when the it_value time elapses:*/
		timer_value.it_interval.tv_usec = 0;                         /*  (in us) */
		timer_value.it_value.tv_sec = interval_stats_sec;            /* Time to the next timer expiration: 1 seconds */
		timer_value.it_value.tv_usec = 0;                            /*  (in us) */
		/* set the interval timer to be decremented in real time */
		
		ret = setitimer( ITIMER_REAL, &timer_value, &timer_old_value );
		if(ret){
			perror("set interval timer failed :" );
			exit(EXIT_FAILURE);
		}
   }

/* record initial interface & snmp info */
	net_snmp_Start(  &net_snmp_stats);

/* get Time-zero for stamping the packets */
	StopWatch_Start(&stopwatch_elapsed);

/* Do Printout */
	/* titles */
	printf("Time sec; ");
	/* print just interface titles */
	net_print_info_file( net_if_info, &snmp_info, 1, 'L', out_file);		
	printf("\n");
	fflush(stdout);

/* loop for ever */
    for(;;){
		sleep(wait_time_int);
    }    /* end of for ever looping */

   return(0);
}


static void parse_command_line (int argc, char **argv)
/* --------------------------------------------------------------------- */
{
/* local variables */
    char c;
    int error;
    int i;
    time_t date;
    char *date_str;

    char *help ={
"Usage: udpmon_bw_mon -option<parameter> [...]\n\
options:\n\
    -A = <cpu core to use - start from 0 >\n\
	-V = print version number\n\
	-a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex>\n\
	-f = output file name [terminal]\n\
	-h = print this message\n\
	-i = <time interval between reading TCP stats sec [10s]>\n\
	-q = quiet - only print results\n\
	"};

    error=0;
    
    while ((c = getopt(argc, argv, "a:f:i:A:hqV")) != (char) EOF) {
	switch(c) {

	    case 'a':
		if (optarg != NULL) {
			//RHJ
			hex2cpuset( &cpu_affinity_cpuset , optarg);
			affinity_cpuset_inuse = 1;
		} else {
		    error = 1;
		}
		break;

	    case 'f':
		if (optarg != NULL) {
		   strncpy(out_filename, optarg, OUTFILENAME_MAXLEN-1);
		   output_file = 1;
		} else {
		    error = 1;
		}
		break;

	    case 'h':
            fprintf (stdout, "%s \n", help);
	        exit(EXIT_SUCCESS);
		break;

	    case 'i':
		if (optarg != NULL) {
		    interval_stats_sec = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'q':
	        quiet = 1;
		break;

	    case 'v':
	        verbose = 1;
		break;
		
	    case 'A':
		if (optarg != NULL) {
		   cpu_affinity_core = atoi(optarg);  
		} else {
		    error = 1;
		}
		break;

	    case 'V':
	        printf(" %s \n", HOSTMON_VERSION);
	        exit(EXIT_SUCCESS);
		break;

	    default:
		break;
	}   /* end of switch */
    }       /* end of while() */

    if (error ) {
	fprintf (stderr, "%s \n", help);
	exit	(EXIT_FAILURE);
    }

    if(!quiet){
        date = time(NULL);
		date_str = ctime(&date);
        date_str[strlen(date_str)-1]=0;
        printf(" %s :", date_str );
        printf(" %s CPUs", HOSTMON_VERSION);
        printf(" Command line: ");
	for(i=0; i<argc; i++){
            printf(" %s", argv[i]);
	}
	printf(" \n");
    }

    return;
}

static void cntlc_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_C */
	fflush(stdout);
	printf("cntl-C received : Process ended\n");
	fflush(stdout);
	exit(EXIT_SUCCESS);

    return;
}
 
static void cntlz_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_Z */
	fflush(stdout);
	printf("cntl-Z received : Process ended\n");
	fflush(stdout);
	exit(EXIT_SUCCESS);

    return;
}

static void sig_alrm( int signo)
/* --------------------------------------------------------------------- */
{
	double delta_t_elapsed;
	int time_alive_sec;
  

/* get the elapsed time now */
	StopWatch_Stop(&stopwatch_elapsed); // time through the transfer
	delta_t_elapsed = StopWatch_TimeDiff(&stopwatch_elapsed);
    time_alive_sec = delta_t_elapsed/1000000 + 0.01 ;  // allow for rounding errors

/* record the incremental network stats */
	net_snmp_Snap(  &net_snmp_stats, net_if_info, &snmp_info );
	net_snmp_Info(  &net_snmp_stats, net_if_info, &snmp_info);

/* print the data */
	printf("%d ;", time_alive_sec);
	net_print_info_file( net_if_info, &snmp_info, 2, 'L', out_file);		

	printf("\n");
	
	return;
}


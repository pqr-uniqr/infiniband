
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>
#include <math.h>
#include "getusage.c"


struct pstat pstart;
struct pstat pend;

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  main
 *  Description:  
 * =====================================================================================
 */
    int
main ( int argc, char *argv[] )
{
    int i, j = 0;
    double ucpu, scpu;

   
    fprintf(stdout, "%d\n",sysconf(_SC_CLK_TCK));
    return 0;

    get_usage( getpid(), &pstart );

    for(i=0; i < 1000000000; i++){
        j++; 
        j--; 
        j = j * 1000;
        j = j / 1000;
        j = sqrt(j);

        if(i % 100000000 == 0) {
            get_usage( getpid(), &pend );
            fprintf(stdout, "pend: utime: %lu, stime: %ld, total_time: %lu\n", 
                    pend.utime_ticks + pend.cutime_ticks, 
                    pend.stime_ticks + pend.cstime_ticks,
                    pend.cpu_total_time);
            calc_cpu_usage_pct( &pend, &pstart, &ucpu, &scpu);
            fprintf(stdout, "ucpu: %f, scpu: %f\n", ucpu, scpu);
        }
    }

    long unsigned int total_time = pend.utime_ticks + pend.stime_ticks;
    
    calc_cpu_usage_pct( &pend, &pstart, &ucpu, &scpu);

    fprintf(stdout, "ucpu: %f, scpu: %f\n", ucpu, scpu);

    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

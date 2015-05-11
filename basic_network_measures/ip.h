#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <math.h>
#include <errno.h>
#include <float.h>

#ifdef NUMA
    #include <numa.h>
#endif

#include "getusage.c"
#include "header.h"

#define NRM  "\x1B[0m"
#define RED  "\x1B[31m"
#define GRN  "\x1B[32m"
#define YEL  "\x1B[33m"
#define BLU  "\x1B[34m"
#define MAG  "\x1B[35m"
#define CYN  "\x1B[36m"
#define WHT  "\x1B[37m"
#define RESET "\033[0m"

#define CPUNO 10

#ifdef DEBUG
# define DEBUG_PRINT(x) fprintf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X) )
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y) )

#define REPORT_FMT "%d  %d  %d  %.2f  %.2f  %.2f  %.2f  %.2f  %.2f\n"

// part 1: threads, xfer_size, iter (setup)
#define MTHREAD_RPT_PT1 "%d  %d  %d"
// part 2: average for bw, lat , ucpu, scpu
#define MTHREAD_RPT_PT2 "%.2f  %.2f  %.2f  %.2f  %.2f  %.2f"

#define MTHREAD_RPT_FMT "%s  %s\n"

#define ALLOCATE(var,type,size)                                  \
    { if((var = (type*)malloc(sizeof(type)*(size))) == NULL)     \
        { fprintf(stderr," Cannot Allocate\n"); exit(1);}}

struct config_t {
    char *server_name;
    u_int32_t tcp_port;
    size_t xfer_unit;
    int iter;
    int threads;
    int length;
    struct config_t *config_other;
};

struct connection{
    int t_num;
    int sock;
    char *buf;
};

struct resources {
    int sock;
    struct connection **conn;
};

struct stats
{
    double max;
    double min;
    double average;
    double median;
};


static int run_iter(void *param);
static void resources_init(struct resources *res);
static int resources_create(struct resources *res);
static int sock_connect(const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);
static uint16_t checksum(void *vdata, size_t length);
static void print_report(void);
static void print_config(void);

static void get_stats(double *data, int size, struct stats *stats);
static int compare_doubles(const void *a, const void *b);

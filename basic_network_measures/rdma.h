#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>

#include "getusage.c"

#define NRM  "\x1B[0m"
#define RED  "\x1B[31m"
#define GRN  "\x1B[32m"
#define YEL  "\x1B[33m"
#define BLU  "\x1B[34m"
#define MAG  "\x1B[35m"
#define CYN  "\x1B[36m"
#define WHT  "\x1B[37m"
#define RESET "\033[0m"


#define MAX_POLL_CQ_TIMEOUT 2000
#define MAX_SEND_WR 100 
#define MAX_RECV_WR 100
#define MAX_SEND_SGE 1
#define MAX_RECV_SGE 1

#define CPUNO 0
#define CQ_SIZE 5

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X) )
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y) )

#define BANDWIDTH 0
#define LATENCY 1


#define REPORT_FMT "%-7d\t%d\t%-7.2f\t%-7.2f\t%7.2f\n"
#define REPORT_FMT_LAT "%-7d\t%d\t%-7.2f\t\n"
#define ALLOCATE(var,type,size)                                  \
    { if((var = (type*)malloc(sizeof(type)*(size))) == NULL)     \
        { fprintf(stderr," Cannot Allocate\n"); exit(1);}}


#ifdef DEBUG
# define DEBUG_PRINT(x) fprintf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

#define CQ_MODERATION 50

struct metrics_t
{
    long unsigned total;
    long unsigned min;
    long unsigned max;
};

// store configuration fed through command line
struct config_t
{
    const char *dev_name;		/* IB device name */
    char *server_name;		/* server host name */
    u_int32_t tcp_port;		/* server TCP port */
    int ib_port;			/* local IB port to work with */
    int gid_idx;			/* gid index to use */
    size_t xfer_unit;       /* how big is each transfer going to be (bytes) */
    int iter;             /* number of times we are going to transfer */
    enum ibv_wr_opcode opcode;     /* requested op */
    int crt;            /* what to test for */
    int threads;        /* number of threads to use */
    int measure;
    struct config_t *config_other;
};

// data for connecting queue pairs
struct cm_con_data_t
{
    uint64_t addr;		/* Buffer address */
    uint32_t rkey;		/* Remote key */
    uint32_t qp_num;		/* QP number */
    uint16_t lid;			/* LID of the IB port */
    uint8_t gid[16];		/* gid */
} __attribute__ ((packed));

// one for every ib connection
struct ib_assets
{
    struct ibv_pd *pd;		/* PD handle */
    struct ibv_cq *cq;		/* CQ handle */
    struct ibv_qp *qp;		/* QP handle */
    struct ibv_mr *mr;		/* MR handle for buf */
    char *buf;			    /* memory buffer pointer */
    struct cm_con_data_t remote_props;	/* values to connect to remote side */
};

struct resources 
{
    /* Device attributes */
    struct ibv_device_attr device_attr;
    struct ibv_port_attr port_attr;	/* IB port attributes */
    struct ibv_context *ib_ctx;	/* device handle */
    int sock;			/* TCP socket file descriptor */
    struct ib_assets **assets;
};



struct proctime_t
{
    double utime;
    double stime;
};


/*  */
static int run_iter_client( void *param );
static int run_iter_server( void *param );

/* QUEUE PAIR STATE MODIFICATION */
static int modify_qp_to_init(struct ibv_qp *qp);
static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid);
static int modify_qp_to_rts(struct ibv_qp *qp);

/* QUEUE PAIR OPERATIONS */
static int connect_qp(struct resources *res);
static int post_receive(struct resources *res);

/* RESOURCE MANAGEMENT */
static void resources_init( struct resources *res);
static int resources_create( struct resources *res);
static int resources_destroy( struct resources *res);
static int conn_destroy( struct ib_assets *conn);

/* SOCKET OPERATION WRAPPERS (TCP) */
static int sock_connect(const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);

/* UTIL */
static void print_report(unsigned int iters, unsigned size, int duplex, int no_cpu_freq_fail);
static void usage(const char *argv0);
static void opcode_to_str(int code, char **str);
static void print_config(void);
static inline uint64_t htonll(uint64_t x);
static inline uint64_t ntohll(uint64_t x);
static void check_wc_status(enum ibv_wc_status status);
static uint16_t checksum(void *vdata, size_t length);

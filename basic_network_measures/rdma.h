#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <byteswap.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <math.h>

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

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X))

// store configuration fed through command line
struct config_t
{
    const char *dev_name;		/* IB device name */
    char *server_name;		/* server host name */
    u_int32_t tcp_port;		/* server TCP port */
    int ib_port;			/* local IB port to work with */
    int gid_idx;			/* gid index to use */
    size_t xfer_unit;       /* how big is each transfer going to be (bytes) */
    size_t xfer_unit_demanded; /* how big is the incoming transfers are going to be (bytes) */
    int trials;             /* number of times we are going to transfer */
    enum ibv_wr_opcode opcode;     /* requested op */
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

// store all resources: device, port, remote connection data, context, protection domain, queue pair, memory registration, data buffer, tcp socket for connection establishment
struct resources 
{
    struct ibv_device_attr device_attr;
    /* Device attributes */
    struct ibv_port_attr port_attr;	/* IB port attributes */
    struct cm_con_data_t remote_props;	/* values to connect to remote side */
    struct ibv_context *ib_ctx;	/* device handle */
    struct ibv_pd *pd;		/* PD handle */
    struct ibv_cq *cq;		/* CQ handle */
    struct ibv_qp *qp;		/* QP handle */
    struct ibv_mr *mr;		/* MR handle for buf */
    char *buf;			/* memory buffer pointer, used for RDMA and send
                           ops */
    int sock;			/* TCP socket file descriptor */
};


/* IB OPERATIONS */
static int post_send(struct resources *res, int opcode);
static int poll_completion(struct resources *res);

/* QUEUE PAIR STATE MODIFICATION */
static int modify_qp_to_init(struct ibv_qp *qp);
static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid);
static int modify_qp_to_rts(struct ibv_qp *qp);

/* QUEUE PAIR OPERATIONS */
static int connect_qp(struct resources *res);
static int post_receive(struct resources *res);

/* RESOURCE MANAGEMENT */
static void resources_init( struct resources *res);
static int resources_create(struct resources *res);
static int resources_destroy( struct resources *res);

/* SOCKET OPERATION WRAPPERS (TCP) */
static int sock_connect(const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);

/* UTIL */
static void usage(const char *argv0);
static void print_config(void);
static inline uint64_t htonll(uint64_t x);
static inline uint64_t ntohll(uint64_t x);
static void check_wc_status(enum ibv_wc_status status);

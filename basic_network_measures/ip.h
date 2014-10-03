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

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X) )

struct config_t
{
    char *server_name;
    u_int32_t tcp_port;
    size_t xfer_unit;
    int trials;
    int ipoib;  //booleanish value
    struct config_t *config_other; //probably not necessary
};

static int sock_connect (const char *servername, int port);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);
static void report_result(float average);
static uint16_t checksum(void *vdata, size_t length);

#include "ip.h"

struct config_t config = {
    NULL, /* server name */
    19875, /* tcp port */
    0,  /* xfer unit */
    0,  /* iterations  */
    NULL,
};

cycles_t cposted;
cycles_t ccompleted;
struct timeval *tposted;
struct timeval *tcompleted;

int main ( int argc, char *argv[] )
{
    int rc = 1;
    struct resources res;
    char temp_char;

    /* PROCES CL ARGUMENTS */

    while(1){
        int c;
        static struct option long_options[] = {
            {.name="xfer-unit", .has_arg=1, .val='b'},
            {.name="iter", .has_arg=1, .val='i'},
            {.name=NULL, .has_arg=0, .val='\0'},
        };

        if( (c = getopt_long(argc, argv, "b:i:c:", long_options, NULL)) == -1 ) break;

        switch(c){
            case 'b':
                config.xfer_unit = pow(2, strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    return 1;
                }
                break;
            case 'i':
                config.iter = strtoul(optarg,NULL,0);
                break;
            default:
                return 1;
        }
    }

    /* PARSE SERVER NAME */
    if (optind == argc-1){
        config.server_name  = argv[optind];
    } else if (optind < argc - 1){
        return 1;
    }

    /* SUM UP CONFIG */
#ifdef DEBUG
    print_config();
#endif

    /* INITIALIZE RESOURCES */
    resources_init(&res);
    DEBUG_PRINT((stdout, GRN "resources_init() successful\n" RESET));

    /* CREATE RESOURCES */
    if( resources_create(&res) ){
        fprintf(stderr, RED "resources_create() failed\n" RESET);
        rc = 1;
        goto main_exit;
    }
    DEBUG_PRINT((stdout, GRN "resources_create() successful\n" RESET));
   
    rc = 0;

    if( sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, "sync error while in data transfer\n");
        return 1;
    }

    run_iter(&res);

    DEBUG_PRINT((stdout, YEL "run_iter finished, headed to final socket sync\n" RESET));

    if( sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, "sync error while in data transfer\n");
        return 1;
    }

    DEBUG_PRINT((stdout, GRN "final socket sync finished--terminating\n" RESET));

    print_report( );

main_exit:

    return EXIT_SUCCESS;
}				

static int run_iter(struct resources *res)
{
    int i;
    int rc;
    int bytes_read;
    uint16_t csum;
    char *read_to;
    int left_to_read;

    DEBUG_PRINT((stdout, YEL "XFER STARTS-------------------\n" RESET ));

    cposted = get_cycles();
    gettimeofday( tposted, NULL );
    for(i = 0; i < config.iter; i++){
        rc = 0;
    
        DEBUG_PRINT((stdout, YEL "ITERATION %d\n" RESET , i));

        if( config.server_name ){

#ifdef DEBUG
            memset(res->buf, i % 2, config.xfer_unit);
            csum = checksum(res->buf, config.xfer_unit);
            DEBUG_PRINT((stdout,WHT "\tchecksum of buffer to be sent: %0x\n" RESET, csum));
#endif

            rc = write(res->sock, res->buf, config.xfer_unit);

            if(rc < config.xfer_unit){
                fprintf(stderr, "Failed writing data to socket in run_iter\n");
                return 1;
            }

            DEBUG_PRINT((stdout, GRN "%d bytes written to socket\n" RESET, rc));
        } else {
            read_to = res->buf;
            left_to_read = config.xfer_unit;
            bytes_read = 0;

            while( left_to_read ){
                rc = read(res->sock, read_to, left_to_read);
    
                if( rc < 0 ){
                    fprintf(stderr, "failed to read from socket in run_iter\n");
                    return 1;
                }
                
                DEBUG_PRINT((stdout, YEL "\t %d bytes read from a call to read()\n", rc));
                left_to_read -= rc;
                bytes_read += rc;
                read_to += rc;
            }

#ifdef DEBUG
            DEBUG_PRINT((stdout, GRN "%d bytes total read from socket\n" RESET, bytes_read));
            csum = checksum(res->buf, bytes_read);
            DEBUG_PRINT((stdout, WHT "\tchecksum on received data = %0x\n" RESET, csum));
#endif
        }
    }
    ccompleted = get_cycles();
    gettimeofday( tcompleted, NULL );

    return 0;

}

static void resources_init(struct resources *res)
{
    memset( (void *) res, 0, sizeof *res);
    res->sock = -1;
}

static int resources_create(struct resources *res)
{
    int rc = 0;
    struct config_t *config_other = (struct config_t *) malloc(sizeof(struct config_t));

    if(config.server_name){
        res->sock = sock_connect(config.server_name, config.tcp_port);
        if(res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection to server %s, port %d\n",
                    config.server_name, config.tcp_port);
            rc = 1;
            goto resources_create_exit;
        }
    } else {
        DEBUG_PRINT((stdout, "waiting on port %d for TCP connection\n", config.tcp_port));
        res->sock = sock_connect(NULL, config.tcp_port);
        if (res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection with client on port %d\n",
                    config.tcp_port);
            rc = 1;
            goto resources_create_exit;
        }
    }
    DEBUG_PRINT((stdout, GRN "TCP connection was established\n" RESET));


    /* EXCHANGE EXPERIMENT CONFIGURATION */
    if( sock_sync_data(res->sock, 
                sizeof(struct config_t), 
                (char *) &config, 
                (char *) config_other) < 0 ){

        fprintf(stderr, "failed to exchange config info\n");
        rc = 1;
        goto resources_create_exit;
    }
    config.xfer_unit = MAX(config.xfer_unit, config_other->xfer_unit);
    config.iter = MAX(config.iter, config_other->iter);
    config.config_other = config_other;
    DEBUG_PRINT((stdout, "buffer %zd bytes, %d iterations\n", config.xfer_unit, config.iter));

    res->buf = (char *) malloc(config.xfer_unit);
    if(!res->buf){
        fprintf(stderr, "failed to malloc res->buf\n");
        rc = 1;
        goto resources_create_exit;
    }
    memset(res->buf, 0x1, config.xfer_unit);

    /* PRINT TCP WINDOW SIZE */

    int tcp_win_size = 0;
    socklen_t len = sizeof( tcp_win_size ); 

    if( config.server_name ){
        getsockopt( res->sock, SOL_SOCKET, SO_SNDBUF, (char *) &tcp_win_size, &len ); //TODO errcheck
        DEBUG_PRINT((stdout, "tcp window size set to %d\n", tcp_win_size));
    } else {
        getsockopt( res->sock, SOL_SOCKET, SO_RCVBUF, (char *) &tcp_win_size, &len ); //TODO errcheck
        DEBUG_PRINT((stdout, "tcp window size set to %d\n", tcp_win_size));
    }

    /* PRINT TCP MAX SEGMENT SIZE */

    int mss = 0;
    len = sizeof( mss );
    getsockopt(res->sock, IPPROTO_TCP, TCP_MAXSEG, (char *) &mss, &len);
    DEBUG_PRINT((stdout, "tcp maximum segment size set to %d\n", mss));

resources_create_exit:
    if(rc){
        if(res->buf){
            free(res->buf);
            res->buf = NULL;
        }
        if(res->sock >= 0){
            if(close(res->sock))
                fprintf(stderr, "failed to close socket\n");
            res->sock = 1;
        }
    }
    return rc;
}

static int sock_connect(const char *servername, int port)
{
    struct addrinfo *resolved_addr = NULL;
    struct addrinfo *iterator;
    char service[6];
    int sockfd = -1;
    int listenfd = 0;
    int tmp;
    int so_reuseaddr = 1;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    if (sprintf (service, "%d", port) < 0)
        goto sock_connect_exit;

    sockfd = getaddrinfo (servername, service, &hints, &resolved_addr);

    if (sockfd < 0)
    {
        fprintf (stderr, "%s for %s:%d\n", gai_strerror (sockfd), servername,
                port);
        goto sock_connect_exit;
    }
    /* Search through results and find the one we want */
    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
    {
        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        setsockopt(sockfd, SOL_SOCKET ,SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

        if (sockfd >= 0){
            if (servername){
                /* Client mode. Initiate connection to remote */
                if ((tmp = connect (sockfd, iterator->ai_addr, iterator->ai_addrlen))){
                    fprintf (stderr, "failed connect \n");
                    close (sockfd);
                    sockfd = -1;
                }
            } else {
                /* Server mode. Set up listening socket an accept a connection */
                listenfd = sockfd;
                sockfd = -1;
                if (bind (listenfd, iterator->ai_addr, iterator->ai_addrlen))
                    goto sock_connect_exit;
                listen (listenfd, 1);
                sockfd = accept (listenfd, NULL, 0);
            }
        }
    }

sock_connect_exit:
    if (listenfd)
        close (listenfd);
    if (resolved_addr)
        freeaddrinfo (resolved_addr);
    if (sockfd < 0)
    {
        if (servername)
            fprintf (stderr, "Couldn't connect to %s:%d\n", servername, port);
        else
        {
            perror ("server accept");
            fprintf (stderr, "accept() failed\n");
        }
    }
    return sockfd;
}

int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;

    rc = write (sock, local_data, xfer_size);

    if (rc < xfer_size)
        fprintf (stderr, "Failed writing data during sock_sync_data\n");
    else
        rc = 0;

    while (!rc && total_read_bytes < xfer_size){
        read_bytes = read (sock, remote_data, xfer_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    return rc;
}

static void print_config( void )
{
    fprintf (stdout, YEL "\n\nCONFIG-------------------------------------------\n" );
    if (config.server_name)
        fprintf (stdout, "IP : %s\n", config.server_name);
    fprintf (stdout, "TCP port : %u\n", config.tcp_port);
    if ( !config.iter|| !config.xfer_unit )
        fprintf(stdout, RED "Size of transfer not specified.\n" YEL );
    else
        fprintf(stdout, "%d iters, each %zd bytes\n", config.iter, config.xfer_unit );
    fprintf (stdout, "CONFIG------------------------------------------\n\n" RESET);
}

static void print_report( void )
{
    double cycles_to_units = get_cpu_mhz(0) * 1000000;
    unsigned size = config.xfer_unit;
    unsigned int iters = config.iter;

    int elapsed = (tcompleted->tv_sec - tposted->tv_sec) / 0x1000000 + 
        (tcompleted->tv_usec - tcompleted->tv_usec);
    printf(REPORT_FMT, size, iters, size * iters / elapsed );
    return;

    printf(REPORT_FMT, size, iters, size * iters * cycles_to_units / 
            (ccompleted - cposted) / 0x100000 );
    return;

    
    // PERFTEST-STYLE BANDWIDTH COMPUTATION
    /*
    double cycles_to_units;
    int i, j;
    int opt_posted = 0, opt_completed = 0;
    unsigned int iters = config.iter;
    unsigned size = config.xfer_unit;
    cycles_t total_delta = 0;
    cycles_t opt_delta;
    cycles_t t;
 
    opt_delta = ccompleted[opt_posted] - cposted[opt_completed];
    //TODO  replace with time
    for (i = 0; i < iters; ++i)
        for (j = i; j < iters; ++j) {
           
            if(i == j){
                total_delta += ccompleted[i] - cposted[i];
            }

            t = (ccompleted[j] - cposted[i]) / (j - i + 1);
            if (t < opt_delta) {
                opt_delta  = t;
                opt_posted = i;
                opt_completed = j;
            }
        }

    // TODO replace with tposted
    cycles_to_units = get_cpu_mhz(0) * 1000000;
    printf(REPORT_FMT, size, iters, size * cycles_to_units / opt_delta / 0x100000,
            //size * iters * cycles_to_units / (total_delta) / 0x100000);
            size * iters * cycles_to_units / (ccompleted[iters-1] - cposted[0]) / 0x100000 );

    // TODO calculate CPU utilization

    */
}

static uint16_t checksum(void *vdata, size_t length)
{
    // Cast the data pointer to one that can be indexed.
    char* data = (char*)vdata;

    // Initialise the accumulator.
    uint32_t acc=0xffff;

    // Handle complete 16-bit blocks.
    size_t i;
    for (i=0;i+1<length;i+=2) {
        uint16_t word;
        memcpy(&word,data+i,2);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Handle any partial block at the end of the data.
    if (length&1) {
        uint16_t word=0;
        memcpy(&word,data+length-1,1);
        acc+=ntohs(word);
        if (acc>0xffff) {
            acc-=0xffff;
        }
    }

    // Return the checksum in network byte order.
    return htons(~acc);
}


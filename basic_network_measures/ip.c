#include "ip.h"

struct config_t config = {
    NULL, /* server name */
    19875, /* tcp port */
    0,  /* xfer unit */
    0,  /* iterations  */
    1, /* threads */
    BANDWIDTH, /* measure */
    NULL,
};

struct timeval tposted;
struct timeval tcompleted;
long int latency;

clock_t startticks;
clock_t endticks;

struct pstat pstart;
struct pstat pend;
struct pstat pstart_server;
struct pstat pend_server;

int cnt_threads;
pthread_mutex_t start_mutex;
pthread_cond_t start_cond;
cpu_set_t cpuset;
pthread_t *threads;

int main ( int argc, char *argv[] )
{
    int rc = 1, i;
    struct resources res;
    char temp_char;
    void * (*functorun)(void *);

    pthread_attr_t attr;
    CPU_ZERO( &cpuset );
    CPU_SET( CPUNO, &cpuset );
    cnt_threads = 0;

    /* PROCES CL ARGUMENTS */

    while(1){
        int c;
        static struct option long_options[] = {
            {.name="xfer-unit", .has_arg=1, .val='b'},
            {.name="iter", .has_arg=1, .val='i'},
            {.name="threads", .has_arg=1, .val='t'},
            {.name="measure", .has_arg=1, .val='m'},
            {.name=NULL, .has_arg=0, .val='\0'},
        };

        if( (c = getopt_long(argc, argv, "b:i:t:m:", long_options, NULL)) == -1 ) break;

        switch(c){
            case 'i':
                config.iter = strtoul(optarg,NULL,0);
                break;
            case 't':
                config.threads = strtoul(optarg, NULL, 0);
                break;
            case 'b':
                config.xfer_unit = pow(2, strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    return 1;
                }
                break;
            case 'm':
                if( ! strcmp(optarg, "l") || ! strcmp(optarg, "lat") || 
                        ! strcmp(optarg, "latency") )
                    config.measure = LATENCY;
                else if( ! strcmp(optarg, "b") || ! strcmp(optarg, "bw") || 
                        ! strcmp(optarg, "bandwidth") )
                    config.measure = BANDWIDTH;
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

    pthread_mutex_init(&start_mutex, NULL);
    pthread_cond_init(&start_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);


    for( i=0; i < config.threads; i++ ){
        if( errno = pthread_create(&threads[i], &attr, 
                    (void *(*)(void*))&run_iter, (void *) res.conn[i]) ){
            perror("pthread_create");
            goto main_exit;
        }
    }

    DEBUG_PRINT((stdout, GRN "threads created\n" RESET));

    do{
        pthread_mutex_lock( &start_mutex );
        i = (cnt_threads < config.threads);
        pthread_mutex_unlock( &start_mutex );
    } while (i);
    DEBUG_PRINT((stdout, GRN "all threads started--signalling start\n" RESET));

    get_usage( getpid(), &pstart, CPUNO );
    gettimeofday( &tposted, NULL );
    startticks = clock();

    pthread_cond_broadcast(&start_cond);
    for(i=0; i < config.threads; i++)
        if(errno = pthread_join(threads[i], NULL)){
            perror("pthread_join");
            goto main_exit;
        }

    endticks = clock();
    gettimeofday( &tcompleted, NULL );
    get_usage( getpid(), &pend, CPUNO );

    DEBUG_PRINT((stdout, GRN "threads joined\n" RESET));
    DEBUG_PRINT((stdout, YEL "run_iter finished, headed to final socket sync\n" RESET));

    if( sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, "sync error while in data transfer\n");
        return 1;
    }

    /* exchange stat data*/
    if( -1 == sock_sync_data( res.sock, sizeof(struct pstat), (char *) &pstart, 
                (char *) &pstart_server)){
        fprintf(stderr, RED "failed to exchange cpu stats\n" RESET);
        goto main_exit;
    }

    if(-1 == sock_sync_data( res.sock, sizeof(struct pstat), (char *) &pend, (char *) &pend_server)){
        fprintf(stderr, RED "failed to exchange cpu stats\n" RESET);
        goto main_exit;
    }

    DEBUG_PRINT((stdout, GRN "final socket sync finished--terminating\n" RESET));

    print_report();

main_exit:
    return EXIT_SUCCESS;
}				

static int run_iter(void * param)
{
    int i, rc, bytes_read, left_to_read;
    uint16_t csum;
    char *read_to;
    struct connection *conn = (struct connection *) param;
    pthread_t thread = pthread_self();

    DEBUG_PRINT((stdout, MAG "[ thread %u ] spawned\n" RESET , (int) thread));

    /* WAIT TO SYNCHRONIZE */

    pthread_mutex_lock( &start_mutex );
    if( errno = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) ){
        perror("pthread_setaffinity");
        return 1;
    }
    cnt_threads++;
    pthread_cond_wait( &start_cond, &start_mutex );
    pthread_mutex_unlock( &start_mutex );

    DEBUG_PRINT((stdout, YEL "XFER STARTS-------------------\n" RESET ));

    for(i = 0; i < config.iter; i++){
        rc = 0;
    
        DEBUG_PRINT((stdout, YEL "ITERATION %d\n" RESET , i));

        if( config.server_name ){

#ifdef DEBUG
            memset(conn->buf, i % 2, config.xfer_unit);
            csum = checksum(conn->buf, config.xfer_unit);
            DEBUG_PRINT((stdout,WHT "\tchecksum of buffer to be sent: %0x\n" RESET, csum));
#endif

            rc = write(conn->sock, conn->buf, config.xfer_unit);

            if(rc < config.xfer_unit){
                fprintf(stderr, "Failed writing data to socket in run_iter\n");
                return 1;
            }

            DEBUG_PRINT((stdout, GRN "%d bytes written to socket\n" RESET, rc));
        } else {
            read_to = conn->buf;
            left_to_read = config.xfer_unit;
            bytes_read = 0;

            while( left_to_read ){
                rc = read(conn->sock, read_to, left_to_read);
    
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
            csum = checksum(conn->buf, bytes_read);
            DEBUG_PRINT((stdout, WHT "\tchecksum on received data = %0x\n" RESET, csum));
#endif
        }
    }
    return 0;
}

static void resources_init(struct resources *res)
{
    memset( (void *) res, 0, sizeof *res);
    res->sock = -1;
}

static int resources_create(struct resources *res)
{
    int rc = 0, i;
    u_int32_t portno = config.tcp_port;
    struct config_t *config_other = (struct config_t *) malloc(sizeof(struct config_t));

    if(config.server_name){
        res->sock = sock_connect(config.server_name, portno);
        if(res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection to server %s, port %d\n",
                    config.server_name, portno);
            return -1;
        }
    } else {
        DEBUG_PRINT((stdout, "waiting on port %d for TCP connection\n", portno));
        res->sock = sock_connect(NULL, portno);
        if (res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection with client on port %d\n",
                    portno);
            return -1;
        }
    }
    DEBUG_PRINT((stdout, GRN "TCP connection was established\n" RESET));


    /* EXCHANGE EXPERIMENT PARAMETERS */
    if( sock_sync_data(res->sock, 
                sizeof(struct config_t), 
                (char *) &config, 
                (char *) config_other) < 0 ){

        fprintf(stderr, "failed to exchange config info\n");
        return -1;
    }
    config.xfer_unit = MAX(config.xfer_unit, config_other->xfer_unit);
    config.iter = MAX(config.iter, config_other->iter);
    config.config_other = config_other;
    config.threads = MAX(config.threads, config_other->threads);
    threads = (pthread_t *) malloc(sizeof(pthread_t) * config.threads);
    DEBUG_PRINT((stdout, "buffer %zd bytes, %d iterations on %d threads\n", 
                config.xfer_unit, config.iter, config.threads));


    /* SET UP TCP CONNECTION AND BUFFER FOR EACH THREAD */

    res->conn = (struct connection **) malloc(sizeof(struct connection *) * 
            config.threads);

    for(i=0; i<config.threads; i++){
        portno++;

        /* SET UP BUFFER */
        DEBUG_PRINT((stdout, "setting up connection and buffer for %dth socket on port %u\n", i, portno));
        res->conn[i] = (struct connection *) malloc(sizeof(struct connection));
        struct connection *c = res->conn[i];
        if( !(c->buf = (char *) malloc( config.xfer_unit )) ){
            fprintf(stderr, "failed to malloc c->buf\n");
            return -1;
        }
        memset(c->buf, 0x1, config.xfer_unit);
        DEBUG_PRINT((stdout, "\tbuffer setup\n"));

        /* ESTABLISH TCP CONNECTION */
        /*  FIXME FIXME FIXME this is a dangerous hack that must be fixed ASAP
         *  in attempting to avoid a race condition of client calling connect()
         *  even before server does accept(), for now we just make the client
         *  wait for a second.
         *  ... don't try this at home
         */

        struct timespec hackpauselen;
        hackpauselen.tv_sec = 0;
        hackpauselen.tv_nsec = 1000000;

        if( ! config.server_name ){
            DEBUG_PRINT((stdout, "\twaiting on port %d for TCP connection\n", portno));
            if( 0 > (c->sock = sock_connect(NULL, portno)) ){
                fprintf(stderr, RED "sock_connect\n" RESET);
                return -1;
            }
        } else {
            nanosleep(&hackpauselen, NULL); //FIXME
            if( 0 > (c->sock = sock_connect(config.server_name, portno))){
                fprintf(stderr, RED "sock_connect\n" RESET);
                return -1;
            }
        }
        DEBUG_PRINT((stdout, "\tTCP connection established\n")); 
    }


    /* PRINT TCP WINDOW SIZE */

    int tcp_win_size = 0;
    socklen_t len = sizeof( tcp_win_size ); 

    if( config.server_name ){
        //TODO errcheck
        getsockopt( res->sock, SOL_SOCKET, SO_SNDBUF, (char *) &tcp_win_size, &len ); 
        DEBUG_PRINT((stdout, "tcp window size set to %d\n", tcp_win_size));
    } else {
        //TODO errcheck
        getsockopt( res->sock, SOL_SOCKET, SO_RCVBUF, (char *) &tcp_win_size, &len ); 
        DEBUG_PRINT((stdout, "tcp window size set to %d\n", tcp_win_size));
    }

    /* PRINT TCP MAX SEGMENT SIZE */

    int mss = 0;
    len = sizeof( mss );
    getsockopt(res->sock, IPPROTO_TCP, TCP_MAXSEG, (char *) &mss, &len);
    DEBUG_PRINT((stdout, "tcp maximum segment size set to %d\n", mss));

    return 0;
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

    if (sockfd < 0){
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
                /* Server mode. Set up listening socket and accept a connection */
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
        fprintf(stdout, "%d iters, each %zd bytes, %d threads\n", 
                config.iter, config.xfer_unit, config.threads );
    fprintf (stdout, "CONFIG------------------------------------------\n\n" RESET);
}

static void print_report( void )
{

    double xfer_total, elapsed, avg_bw, avg_lat,
           ucpu=0.,scpu=0.,ucpu_server=0.,scpu_server=0.;

    xfer_total = config.xfer_unit * config.iter * config.threads;
    elapsed = (tcompleted.tv_sec * 1e6 + tcompleted.tv_usec) -
        (tposted.tv_sec * 1e6 + tposted.tv_usec);
    avg_bw = xfer_total / elapsed;
    avg_lat = elapsed / config.iter;

    // to avoid NaN's
    if( pend.cpu_total_time - pstart.cpu_total_time )
        calc_cpu_usage_pct(&pend, &pstart, &ucpu, &scpu);
    if( pend_server.cpu_total_time - pstart_server.cpu_total_time )
        calc_cpu_usage_pct(&pend_server, &pstart_server, &ucpu_server, &scpu_server);

    // format: threads, transfer unit, iterations, avg_bw, avg_lat, ucpu,scpu,ucpuS,scpuS
    printf(REPORT_FMT, config.threads, config.xfer_unit, config.iter, 
            avg_bw, avg_lat, ucpu, scpu, ucpu_server, scpu_server);
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


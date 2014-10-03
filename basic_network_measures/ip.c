/* author:hk110
 * network benchmark with normal TCP/IP socket and IPoIB
 * 
 * */



#include "ip.h"

/* GLOBALS */
struct config_t config=
{
    NULL, /* server name */
    19876, /* tcp port */
    0,  /* xfer unit */
    0,  /* trials */
    0, /* ipoib */
    NULL /* other config */
};

int main ( int argc, char *argv[] )
{
    int i;
    int sock;
    int trials;
    char temp_char;
    char *buf;
    int written_bytes;
    int read_bytes;
    int total_read_bytes;
    struct timeval cur_time;
    long start_time_usec;
    long cur_time_usec;
    long average = 0;
    uint16_t csum;


    while (1)
    {
        int c;
        static struct option long_options[] = {
            {.name = "port",.has_arg = 1,.val = 'p'},
            {.name = "xfer-unit", .has_arg = 1, .val = 'b'},
            {.name = "trials", .has_arg = 1, .val = 't'},
            {.name = "ipoib", .has_arg=0, .val= 'i'},
            {.name = NULL,.has_arg = 0,.val = '\0'}
        };

        if( (c = getopt_long(argc, argv, "p:b:t:i:", long_options, NULL)) == -1) break;

        switch(c){
            case 'p':
                config.tcp_port = strtoul (optarg, NULL, 0);
                break;
            case 'b':
                config.xfer_unit = pow(2,strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    return 1;
                }
                break;
            case 't':
                config.trials = strtoul(optarg, NULL, 0);
                if(config.trials < 0){
                    return 1;
                }
                break;
            case 'i':
                config.ipoib = 1;
            default:
                return 1;
        }

    }

    /* PARSE SERVER NAME IF GIVEN*/
    if (optind == argc - 1){
        config.server_name = argv[optind];
    } else if (optind < argc) {
        return 1;
    }

    sock = -1;

    if(config.server_name){
        sock = sock_connect(config.server_name, config.tcp_port);
        if(sock < 0){
            fprintf(stderr, "failed to establish TCP connection to server %s, port %d\n",
                    config.server_name, config.tcp_port);
            goto main_exit;
        }
    } else {
        fprintf (stdout, "waiting on port %d for TCP connection\n", config.tcp_port);
        sock = sock_connect(NULL, config.tcp_port);
        if (sock < 0){
            fprintf (stderr, "failed to establish TCP connection with client on port %d\n",
                    config.tcp_port);
            goto main_exit;
        }
    }


    //exchange config info 
    struct config_t *config_other = (struct config_t *) malloc( sizeof(struct config_t) );
    sock_sync_data(sock, sizeof(struct config_t), (char *) &config, (char *) config_other);
    config.xfer_unit= MAX(config.xfer_unit, config_other->xfer_unit);
    config.config_other = config_other;

    trials = MAX( config.trials, config.config_other->trials );
 
    buf = (char *) malloc( config.xfer_unit);
    memset(buf, 0, config.xfer_unit);

    for(i = 0; i< trials; i++){
#ifdef DEBUG
        fprintf(stdout, YEL "trial no. %d ------------\n" RESET , i);
#endif
        FILE *random = fopen("/dev/urandom","r");
        fread(buf, 1, config.xfer_unit, random);
        fclose(random);

        if( !config.server_name ){
            gettimeofday(&cur_time, NULL);
            start_time_usec = (cur_time.tv_sec * 1000 * 1000) + cur_time.tv_usec;
            
            written_bytes = write(sock, buf, config.xfer_unit);
            if( written_bytes >= config.xfer_unit){
                gettimeofday(&cur_time, NULL);
                cur_time_usec = (cur_time.tv_sec * 1000 * 1000) + cur_time.tv_usec;
                average += (cur_time_usec - start_time_usec);
            } else {
                goto main_exit;
            }
        } else {
            while(total_read_bytes < config.xfer_unit){
                read_bytes = read( sock, buf, config.xfer_unit);
                total_read_bytes += read_bytes;
            }
        }
#ifdef DEBUG
        csum = checksum( buf, config.xfer_unit );
        printf(WHT "checksum of data in my buffer: %0x\n" RESET, csum);
#endif

        report_result((float) average / (float) trials);

        sock_sync_data(sock, 1, "R", &temp_char);
    }




main_exit:
    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */


static void report_result(float average)
{
    fprintf(stdout, "Report:\n");
    fprintf(stdout, "Trials: %d, Transfer Unit: %zd bytes\n", config.trials, config.xfer_unit);
    fprintf(stdout, "Average time/trial: %f microseconds\n" , average);

}

static int sock_connect (const char *servername, int port)
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
        fprintf (stderr, "%s for %s:%d\n", gai_strerror (sockfd), servername, port);
        goto sock_connect_exit;
    }

    /* Search through results and find the one we want */
    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
    {
        void *addr;
        char *ipver;
        char ipstr[INET6_ADDRSTRLEN];
        if (iterator->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)iterator->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)iterator->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }
        // convert the IP to a string and print it:
        inet_ntop(iterator->ai_family, addr, ipstr, sizeof ipstr);
        printf("%s: %s\n", ipver, ipstr); 


        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        setsockopt(sockfd, SOL_SOCKET ,SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

        if (sockfd >= 0){
            if (servername){
                // Client mode. Initiate connection to remote
                if (( tmp = connect (sockfd, iterator->ai_addr, iterator->ai_addrlen) )){
                    fprintf (stderr, "failed connect \n");
                    close (sockfd);
                    sockfd = -1;
                }
            } else {
                // Server mode. Set up listening socket an accept a connection

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



int sock_sync_data (int sock, int xfer_size, char *local_data, char *remote_data)
{
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;

    rc = write (sock, local_data, xfer_size);

    if (rc < xfer_size)
        fprintf (stderr, "Failed writing data during sock_sync_data\n");
    else
        rc = 0;

    while (!rc && total_read_bytes < xfer_size)
    {
        read_bytes = read (sock, remote_data, xfer_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    return rc;
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


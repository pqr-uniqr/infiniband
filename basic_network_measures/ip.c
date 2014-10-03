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
    int sock;

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

    //print_config(); ///TODO 

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

main_exit:
    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */



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

        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        setsockopt(sockfd, SOL_SOCKET ,SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

        if (sockfd >= 0)
        {
            if (servername)
            {
                /* Client mode. Initiate connection to remote */
                if (( tmp = connect (sockfd, iterator->ai_addr, iterator->ai_addrlen) ))
                {
                    fprintf (stderr, "failed connect \n");
                    close (sockfd);
                    sockfd = -1;
                }
            }
            else
            {
                /* Server mode. Set up listening socket an accept a connection */

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

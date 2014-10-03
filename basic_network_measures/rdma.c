/* author: hk110
 * rdma_mellanox_example.c modified for use in performance benchmark
 * measures: network bandwidth, latency, cpu usage 
 * */

#include "rdma.h"



/* GLOBALS */

struct config_t config = 
{
    NULL,               /* dev_name */
    NULL,               /* server_name */
    19875,          /* tcp_port */
    1,              /* ib_port */
    -1              /* gid_idx */
};
int msg_size;
char *msg;


int main ( int argc, char *argv[] )
{

    int rc = 1;
    int i;
    struct resources res;
    size_t data_len_bytes = 0; //FIXME your naming sucks

    /* PROCESS CL ARGUMENTS */

    while (1)
    {
        int c;
        static struct option long_options[] = {
            {.name = "port",.has_arg = 1,.val = 'p'},
            {.name = "ib-dev",.has_arg = 1,.val = 'd'},
            {.name = "ib-port",.has_arg = 1,.val = 'i'},
            {.name = "gid-idx",.has_arg = 1,.val = 'g'},
            {.name = "bytes", .has_arg = 1, .val = 'b'},
            {.name = NULL,.has_arg = 0,.val = '\0'}
        };

        if( (c = getopt_long(argc,argv, "p:d:i:g:b:", long_options, NULL)) == -1 ) break;

        switch (c)
        {
            case 'p':
                config.tcp_port = strtoul (optarg, NULL, 0);
                break;
            case 'd':
                config.dev_name = strdup (optarg);
                break;
            case 'i':
                config.ib_port = strtoul (optarg, NULL, 0);
                if (config.ib_port < 0)
                {
                    usage (argv[0]);
                    return 1;
                }
                break;
            case 'g':
                config.gid_idx = strtoul (optarg, NULL, 0);
                if (config.gid_idx < 0)
                {
                    usage (argv[0]);
                    return 1;
                }
                break;
            case 'b':
                data_len_bytes = (int) pow( (double) 2, (double) strtoul(optarg, NULL, 0));
                if( data_len_bytes < 0)
                {
                    usage(argv[0]);
                    return 1;
                }
                break;
            default:
                usage (argv[0]);
                return 1;
        }
    }

    
    /* PARSE SERVER NAME IF GIVEN*/
    if (optind == argc - 1)
    {
        config.server_name = argv[optind];
    }
    else if (optind < argc)
    {
        usage (argv[0]);
        return 1;
    }

    /* GENERATE DATA IF REQUESTED */
    if( data_len_bytes > 0 ){
        printf("Generating %zd bytes to send...\n", data_len_bytes);
        msg = (char *) malloc(data_len_bytes);
        msg_size = data_len_bytes;
        FILE *fp = fopen("/dev/urandom", "r");
        fread(msg, 1, msg_size, fp);
        fclose(fp);

        if(!config.server_name){
            printf("This is the server: will use SEND/RECV\n");
        } else {
            printf("This is the client: will use RDMA RW\n");
        }
#ifdef DEBUG
        printf("data to be sent:\n" RED);
        for(i=0; i< data_len_bytes; i++){
            printf("%0x",msg[i]);
        }
        printf("\n" RESET );
#endif
    }

    /* SUM UP CONFIG */
    print_config();

    /* INITIATE RESOURCES  */
    resources_init(&res);
#ifdef DEBUG
    printf(GRN "resources_init() successful\n" RESET);
#endif

    /* SET UP RESOURCES  */
    if( resources_create(&res) ){
        fprintf(stderr, RED "resources_create() failed\n" RESET);
        rc = 1;
        goto main_exit;

    }
#ifdef DEBUG
    printf(GRN "resources_create() successful\n" RESET);
#endif

    /* CONNECT QUEUE PAIRS */
    if( connect_qp(&res) ){
        fprintf(stderr, RED "connect_qp() failed\n" RESET);
        rc = 1;
        goto main_exit;
    }
#ifdef DEBUG
    printf(GRN "connect_qp() successful\n" RESET);
#endif


    rc = 0;

main_exit:
    if (resources_destroy (&res)){
        fprintf (stderr, "failed to destroy resources\n");
        rc = 1;
    }
    if (config.dev_name) free ((char *) config.dev_name);

    fprintf (stdout, "\ntest result is %d\n", rc);
    free( msg );
    return rc;

}				/* ----------  end of function main  ---------- */


/* QUEUE PAIR STATE MODIFICATION */

static int modify_qp_to_init (struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = config.ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE;
    flags =
        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    rc = ibv_modify_qp (qp, &attr, flags);
    if (rc)
        fprintf (stderr, "failed to modify QP state to INIT\n");
    return rc;
}

static int modify_qp_to_rtr (struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t * dgid)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_256;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = config.ib_port;
    if (config.gid_idx >= 0)
    {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = 1;
        memcpy (&attr.ah_attr.grh.dgid, dgid, 16);
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.sgid_index = config.gid_idx;
        attr.ah_attr.grh.traffic_class = 0;
    }
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    rc = ibv_modify_qp (qp, &attr, flags);
    if (rc)
        fprintf (stderr, "failed to modify QP state to RTR\n");
    return rc;
}

static int modify_qp_to_rts (struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 6;
    attr.rnr_retry = 0;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    rc = ibv_modify_qp (qp, &attr, flags);
    if (rc)
        fprintf (stderr, "failed to modify QP state to RTS\n");
    return rc;
}

static int connect_qp (struct resources *res)
{
    struct cm_con_data_t local_con_data;
    struct cm_con_data_t remote_con_data;
    struct cm_con_data_t tmp_con_data;
    int rc = 0;
    char temp_char;
    union ibv_gid my_gid;


    /* GET AN INDEX FROM THE PORT, IF SPECIFIED */
    if (config.gid_idx >= 0){
        rc =
            ibv_query_gid (res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
        if (rc)
        {
            fprintf (stderr, "could not get gid for port %d, index %d\n",
                    config.ib_port, config.gid_idx);
            return rc;
        }
    } else {
        memset (&my_gid, 0, sizeof my_gid);
    }


    /* EXCHANGE IB CONNECTION DATA USING TCP SOCKETS */
    local_con_data.addr = htonll ((uintptr_t) res->buf);
    local_con_data.rkey = htonl (res->mr->rkey);
    local_con_data.qp_num = htonl (res->qp->qp_num);
    local_con_data.lid = htons (res->port_attr.lid);
    memcpy (local_con_data.gid, &my_gid, 16);
    fprintf (stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid);

    if (sock_sync_data
            (res->sock, sizeof (struct cm_con_data_t), (char *) &local_con_data,
             (char *) &tmp_con_data) < 0)
    {
        fprintf (stderr, "failed to exchange connection data between sides\n");
        rc = 1;
        goto connect_qp_exit;
    }


    /* SAVE REMOTE CONNECTION DATA  */
    remote_con_data.addr = ntohll (tmp_con_data.addr);
    remote_con_data.rkey = ntohl (tmp_con_data.rkey);
    remote_con_data.qp_num = ntohl (tmp_con_data.qp_num);
    remote_con_data.lid = ntohs (tmp_con_data.lid);
    memcpy (remote_con_data.gid, tmp_con_data.gid, 16);
    res->remote_props = remote_con_data;
    fprintf (stdout, "Remote address = 0x%" PRIx64 "\n", remote_con_data.addr);
    fprintf (stdout, "Remote rkey = 0x%x\n", remote_con_data.rkey);
    fprintf (stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num);
    fprintf (stdout, "Remote LID = 0x%x\n", remote_con_data.lid);
    if (config.gid_idx >= 0)
    {
        uint8_t *p = remote_con_data.gid;
        fprintf (stdout,
                "Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                p[10], p[11], p[12], p[13], p[14], p[15]);
    }




    /* MODIFY QP STATE TO INIT */
    rc = modify_qp_to_init (res->qp);
    if (rc)
    {
        fprintf (stderr, "change QP state to INIT failed\n");
        goto connect_qp_exit;
    }

    /* */
    if (config.server_name)
    {
        rc = post_receive (res);
        if (rc)
        {
            fprintf (stderr, "failed to post RR\n");
            goto connect_qp_exit;
        }
    }


    /* modify the QP to RTR */
    rc =
        modify_qp_to_rtr (res->qp, remote_con_data.qp_num, remote_con_data.lid,
                remote_con_data.gid);
    if (rc)
    {
        fprintf (stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }
    fprintf (stderr, "Modified QP state to RTR\n");
    rc = modify_qp_to_rts (res->qp);
    if (rc)
    {
        fprintf (stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }
    fprintf (stdout, "QP state was change to RTS\n");
    /* sync to make sure that both sides are in states that they can connect to prevent packet loose */
    if (sock_sync_data (res->sock, 1, "Q", &temp_char))	/* just send a dummy char back and forth */
    {
        fprintf (stderr, "sync error after QPs are were moved to RTS\n");
        rc = 1;
    }
connect_qp_exit:
    return rc;
}

static int post_receive (struct resources *res)
{
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_wr;
    int rc;
    /* prepare the scatter/gather entry */
    memset (&sge, 0, sizeof (sge));
    sge.addr = (uintptr_t) res->buf;
    sge.length = MSG_SIZE;
    sge.lkey = res->mr->lkey;
    /* prepare the receive work request */
    memset (&rr, 0, sizeof (rr));
    rr.next = NULL;
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;
    /* post the Receive Request to the RQ */
    rc = ibv_post_recv (res->qp, &rr, &bad_wr);
    if (rc)
        fprintf (stderr, "failed to post RR\n");
    else
        fprintf (stdout, "Receive Request was posted\n");
    return rc;
}

/* RESOURCE MANAGEMENT */

static void resources_init (struct resources *res)                                         
{
    memset (res, 0, sizeof *res);                                              
    res->sock = -1; 
}   

static int resources_create (struct resources *res)
{
    struct ibv_device **dev_list = NULL;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_device *ib_dev = NULL;
    size_t size;
    int i;
    int mr_flags = 0;
    int cq_size = 0;
    int num_devices;
    int rc = 0;

    /* ESTABLISH TCP CONNECTION */
    if (config.server_name) {
        res->sock = sock_connect(config.server_name, config.tcp_port);
        if (res->sock < 0)
        {
            fprintf (stderr,
                    "failed to establish TCP connection to server %s, port %d\n",
                    config.server_name, config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
    } else {
        fprintf (stdout, "waiting on port %d for TCP connection\n",
                config.tcp_port);
        res->sock = sock_connect (NULL, config.tcp_port);
        if (res->sock < 0)
        {
            fprintf (stderr,
                    "failed to establish TCP connection with client on port %d\n",
                    config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
    }
    fprintf (stdout, "TCP connection was established\n");



    /* GET IB DEVICES AND SELECT ONE */
    fprintf (stdout, "searching for IB devices in host\n");
    dev_list = ibv_get_device_list (&num_devices);
    if (!dev_list)
    {
        fprintf (stderr, "failed to get IB devices list\n");
        rc = 1;
        goto resources_create_exit;
    }
    if (!num_devices)
    {
        fprintf (stderr, "found %d device(s)\n", num_devices);
        rc = 1;
        goto resources_create_exit;
    }
    fprintf (stdout, "found %d device(s)\n", num_devices);
    for (i = 0; i < num_devices; i++)
    {
        if (!config.dev_name)
        {
            config.dev_name = strdup (ibv_get_device_name (dev_list[i]));
            fprintf (stdout,
                    "device not specified, using first one found: %s\n",
                    config.dev_name);
        }
        if (!strcmp (ibv_get_device_name (dev_list[i]), config.dev_name))
        {
            ib_dev = dev_list[i];
            break;
        }
    }
    if (!ib_dev)
    {
        fprintf (stderr, "IB device %s wasn't found\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }
    res->ib_ctx = ibv_open_device (ib_dev);
    if (!res->ib_ctx)
    {
        fprintf (stderr, "failed to open device %s\n", config.dev_name);
        rc = 1;
        goto resources_create_exit;
    }
    ibv_free_device_list (dev_list);
    dev_list = NULL;
    ib_dev = NULL;


    /* GET LOCAL PORT PROPERTIES */
    if (ibv_query_port (res->ib_ctx, config.ib_port, &res->port_attr))
    {
        fprintf (stderr, "ibv_query_port on port %u failed\n", config.ib_port);
        rc = 1;
        goto resources_create_exit;
    }

    /* ALLOCATE PROTECTION DOMAIN */
    res->pd = ibv_alloc_pd (res->ib_ctx);
    if (!res->pd)
    {
        fprintf (stderr, "ibv_alloc_pd failed\n");
        rc = 1;
        goto resources_create_exit;
    }

    /* CREATE COMPLETION QUEUE */
    cq_size = 1;
    res->cq = ibv_create_cq (res->ib_ctx, cq_size, NULL, NULL, 0);
    if (!res->cq)
    {
        fprintf (stderr, "failed to create CQ with %u entries\n", cq_size);
        rc = 1;
        goto resources_create_exit;
    }

    /* CREATE MEMORY BUFFER */
    size = msg_size;
    res->buf = (char *) malloc (msg_size);
    if (!res->buf)
    {
        fprintf (stderr, "failed to malloc %Zu bytes to memory buffer\n", size);
        rc = 1;
        goto resources_create_exit;
    }
    memset (res->buf, 0, size);

    /* REGISTER MEMORY BUFFER */
    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE;
    res->mr = ibv_reg_mr (res->pd, res->buf, size, mr_flags);
    if (!res->mr)
    {
        fprintf (stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
        rc = 1;
        goto resources_create_exit;
    }
    fprintf (stdout,
            "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
            res->buf, res->mr->lkey, res->mr->rkey, mr_flags);


    /* CREATE QUEUE PAIR */
    memset (&qp_init_attr, 0, sizeof (qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = res->cq;
    qp_init_attr.recv_cq = res->cq;
    qp_init_attr.cap.max_send_wr = 1;
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    res->qp = ibv_create_qp (res->pd, &qp_init_attr);
    if (!res->qp)
    {
        fprintf (stderr, "failed to create QP\n");
        rc = 1;
        goto resources_create_exit;
    }
    fprintf (stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num);

    /* FOR WHEN THINGS GO WRONG */
resources_create_exit:
    if (rc)
    {
        /* Error encountered, cleanup */
        if (res->qp)
        {
            ibv_destroy_qp (res->qp);
            res->qp = NULL;
        }
        if (res->mr)
        {
            ibv_dereg_mr (res->mr);
            res->mr = NULL;
        }
        if (res->buf)
        {
            free (res->buf);
            res->buf = NULL;
        }
        if (res->cq)
        {
            ibv_destroy_cq (res->cq);
            res->cq = NULL;
        }
        if (res->pd)
        {
            ibv_dealloc_pd (res->pd);
            res->pd = NULL;
        }
        if (res->ib_ctx)
        {
            ibv_close_device (res->ib_ctx);
            res->ib_ctx = NULL;
        }
        if (dev_list)
        {
            ibv_free_device_list (dev_list);
            dev_list = NULL;
        }
        if (res->sock >= 0)
        {
            if (close (res->sock))
                fprintf (stderr, "failed to close socket\n");
            res->sock = -1;
        }
    }

    return rc;
}

static int resources_destroy (struct resources *res)
{
    int rc = 0;
    if (res->qp)
        if (ibv_destroy_qp (res->qp))
        {
            fprintf (stderr, "failed to destroy QP\n");
            rc = 1;
        }
    if (res->mr)
        if (ibv_dereg_mr (res->mr))
        {
            fprintf (stderr, "failed to deregister MR\n");
            rc = 1;
        }
    if (res->buf)
        free (res->buf);
    if (res->cq)
        if (ibv_destroy_cq (res->cq))
        {
            fprintf (stderr, "failed to destroy CQ\n");
            rc = 1;
        }
    if (res->pd)
        if (ibv_dealloc_pd (res->pd))
        {
            fprintf (stderr, "failed to deallocate PD\n");
            rc = 1;
        }
    if (res->ib_ctx)
        if (ibv_close_device (res->ib_ctx))
        {
            fprintf (stderr, "failed to close device context\n");
            rc = 1;
        }
    if (res->sock >= 0)
        if (close (res->sock))
        {
            fprintf (stderr, "failed to close socket\n");
            rc = 1;
        }
    return rc;
}

/* SOCKET OPERATION WRAPPERS */

static int sock_connect (const char *servername, int port)
{
    struct addrinfo *resolved_addr = NULL;
    struct addrinfo *iterator;
    char service[6];
    int sockfd = -1;
    int listenfd = 0;
    int tmp;
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
        sockfd =
            socket (iterator->ai_family, iterator->ai_socktype,
                    iterator->ai_protocol);
        if (sockfd >= 0)
        {
            if (servername)
            {
                /* Client mode. Initiate connection to remote */
                if ((tmp =
                            connect (sockfd, iterator->ai_addr, iterator->ai_addrlen)))
                {
                    fprintf (stdout, "failed connect \n");
                    close (sockfd);
                    sockfd = -1;
                }
            }
            else
            {
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

/* UTILITY */

static void usage (const char *argv0)
{

    fprintf (stdout, "Usage:\n");
    fprintf (stdout, " %s start a server and wait for connection\n", argv0);
    fprintf (stdout, " %s <host> connect to server at <host>\n", argv0);
    fprintf (stdout, "\n");
    fprintf (stdout, "Options:\n");
    fprintf (stdout,
            " -p, --port <port> listen on/connect to port <port> (default 18515)\n");
    fprintf (stdout,
            " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
    fprintf (stdout,
            " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
    fprintf (stdout,
            " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
}

static void print_config (void)
{
    fprintf (stdout, YEL "CONFIG-------------------------------------------\n" );
    fprintf (stdout, "Device name : \"%s\"\n", config.dev_name);
    fprintf (stdout, "IB port : %u\n", config.ib_port);
    if (config.server_name)
        fprintf (stdout, "IP : %s\n", config.server_name);
    fprintf (stdout, "TCP port : %u\n", config.tcp_port);
    if (config.gid_idx >= 0)
        fprintf (stdout, "GID index : %u\n", config.gid_idx);
    fprintf (stdout, "CONFIG------------------------------------------\n\n" RESET);
}


#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll (uint64_t x)
{
    return bswap_64 (x);
}

static inline uint64_t ntohll (uint64_t x)
{
    return bswap_64 (x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN

static inline uint64_t htonll (uint64_t x)
{
    return x;
}

static inline uint64_t ntohll (uint64_t x)
{
    return x;
}
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif 

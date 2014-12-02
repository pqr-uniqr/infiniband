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
    -1,              /* gid_idx */
    0,              /* xfer_unit */
    0,              /* iter */
    -1,             /* opcode */
    CRT_DEF,       /* criteria */
    1,              /* number of threads */
    NULL,
};

struct timeval tposted; 
struct timeval tcompleted;

struct pstat pstart;
struct pstat pend;

int cnt_threads;
pthread_mutex_t start_mutex;
pthread_cond_t start_cond;
cpu_set_t cpuset;
pthread_t *threads;

/* MAIN */
int
main ( int argc, char *argv[] )
{
    int i, iter, rc = 0;
    uint16_t csum;
    struct resources res;
    char temp_char;
    struct timeval cur_time;

    pthread_attr_t attr;
    CPU_ZERO( &cpuset );
    CPU_SET( CPUNO, &cpuset );
    cnt_threads = 0;

    /* PROCESS CL ARGUMENTS */

    while (1)
    {
        int c;
        static struct option long_options[] = {
            {.name = "port",.has_arg = 1,.val = 'p'},
            {.name = "ib-dev",.has_arg = 1,.val = 'd'},
            //{.name = "ib-port",.has_arg = 1,.val = 'i'},
            {.name = "gid-idx",.has_arg = 1,.val = 'g'},
            {.name = "xfer-unit", .has_arg = 1, .val = 'b'},
            {.name = "iter", .has_arg = 1, .val = 'i'},
            {.name = "verb", .has_arg=1, .val= 'v'},
            {.name = "criteria", .has_arg=1, .val='c'},
            {.name = "threads", .has_arg = 1, .val='t'},
            {.name = NULL,.has_arg = 0,.val = '\0'},
        };

        if( (c = getopt_long(argc,argv, "p:d:g:b:i:v:c:t:", long_options, NULL)) == -1 ) break;

        switch (c)
        {
            case 'p':
                config.tcp_port = strtoul (optarg, NULL, 0);
                break;
            case 'd':
                config.dev_name = strdup (optarg);
                break;
            case 'g':
                config.gid_idx = strtoul (optarg, NULL, 0);
                if (config.gid_idx < 0){
                    usage (argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'b':
                config.xfer_unit = pow(2,strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'i':
                config.iter = strtoul(optarg, NULL, 0);
                if(config.iter < 0){
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'v':
                //awkward!
                if( 'r' == optarg[0] ){
                    config.opcode = IBV_WR_RDMA_READ;
                } else if( 'w' == optarg[0] ){
                    config.opcode = IBV_WR_RDMA_WRITE;
                } else if( 's' == optarg[0] ){
                    config.opcode = IBV_WR_SEND; 
                } else {
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'c':
                //awkward!
                if(optarg[0] == 'b'){
                    config.crt = CRT_BW;
                } else if( optarg[0] == 'l' ){
                    config.crt = CRT_LAT;
                } else if( optarg[0] == 'c'){
                    config.crt = CRT_CPU;
                } else {
                    config.crt = CRT_DEF;
                }
                break;
            case 't':
                config.threads = strtoul(optarg, NULL, 0);
                if( config.threads < 0){
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            default:
                usage (argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* PARSE SERVER NAME IF GIVEN*/
    if (optind == argc - 1){
        config.server_name = argv[optind];
    } else if (optind < argc - 1){
        usage (argv[0]);
        return EXIT_FAILURE;
    }

    /* SUM UP CONFIG */
#ifdef DEBUG
    print_config();
#endif

    /* INITIALIZE RESOURCES  */
    resources_init( &res );
    DEBUG_PRINT((stdout, GRN "resources_init() successful\n" RESET));

    // FROM HERE ON: functions containing malloc -----------------------------------
    
    if( -1 == resources_create(&res) ){
        fprintf(stderr , RED "resources_create\n" RESET);
        goto main_exit;
    }
    DEBUG_PRINT((stdout,GRN "resources_create() successful\n" RESET));

    if( -1 == connect_qp(&res) ){
        fprintf(stderr, RED "connect_qp\n" RESET);
        goto main_exit;
    }
    DEBUG_PRINT((stdout, GRN "connect_qp() successful\n" RESET));

    pthread_mutex_init(&start_mutex, NULL);
    pthread_cond_init(&start_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if( config.opcode != -1 ){
        for(i=0; i < config.threads; i++){
            if( errno = pthread_create( &threads[i], &attr, 
                        (void * (*)(void *)) run_iter, (void *) res.assets[i]) ){
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
        

        /* SIGNAL THREADS TO START WORK */
        pthread_cond_broadcast(&start_cond);
        for(i=0; i < config.threads; i++)
            if(errno = pthread_join(threads[i], NULL)){
                perror("pthread_join");
                goto main_exit;
            }

        gettimeofday( &tcompleted, NULL );
        get_usage( getpid(), &pend, CPUNO );

        DEBUG_PRINT((stdout, GRN "threads joined\n" RESET));
    }

    if( -1 == sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, RED "final sync failed\n" RESET);
        goto main_exit;
    }
    DEBUG_PRINT((stdout, GRN "final socket sync finished--terminating\n" RESET));

    rc = 1;

main_exit:
    if( -1 == resources_destroy(&res) )
        fprintf(stderr, RED "resources_destroy\n" RESET);

    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&start_mutex);
    pthread_cond_destroy(&start_cond);

    if (config.dev_name) free ((char *) config.dev_name);
    if (config.config_other) free((char *) config.config_other);
    if (threads) free((char *) threads);

    /* REPORT ON EXPERIMENT TO STDOUT */

    if(rc){
        print_report(config.iter, config.xfer_unit, 0, 0);
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

static int
run_iter(void *param)
{

    /* DECLARE AND INITIALIZE */

    int rc, scnt=0, ccnt=0, ne, i;

    struct ibv_send_wr sr, *bad_wr=NULL;
    struct ibv_wc *wc;
    struct ibv_sge sge;
    struct ib_assets *conn = (struct ib_assets *) param;
    pthread_t thread = pthread_self();

    DEBUG_PRINT((stdout, "[thread %u] ready\n", (int) thread));

    ALLOCATE(wc, struct ibv_wc, 1);

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = config.opcode;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t) conn->buf;
    sge.length = config.xfer_unit;
    sge.lkey = conn->mr->lkey;

    if( config.opcode != IBV_WR_SEND ){
        sr.wr.rdma.remote_addr = conn->remote_props.addr;
        sr.wr.rdma.rkey = conn->remote_props.rkey;
    }

    /* WAIT TO SYNCHRONIZE */
    
    pthread_mutex_lock( &start_mutex );
    if( errno = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) ){
        perror("pthread_setaffinity");
        return -1;
    }
    cnt_threads++;
    pthread_cond_wait( &start_cond, &start_mutex );
    pthread_mutex_unlock( &start_mutex );

    DEBUG_PRINT((stdout, "[thread %u] starting\n", (int) thread));

    /* GO! */
    while( scnt < config.iter || ccnt < config.iter ){

        while( scnt < config.iter && (scnt - ccnt) < MAX_SEND_WR ){

            if((scnt % CQ_MODERATION) == 0)
                sr.send_flags &= ~IBV_SEND_SIGNALED;

            if( ( errno = ibv_post_send(conn->qp, &sr, &bad_wr) ) ){
                perror("post_send");
                return -1;
            }

            ++scnt;

            if( scnt % CQ_MODERATION == CQ_MODERATION -1 || scnt == config.iter - 1 )
                sr.send_flags |= IBV_SEND_SIGNALED;
        }

        if( ccnt < config.iter ){
            do {
                ne = ibv_poll_cq(conn->cq, 1, wc);
                if( ne > 0 ){
                    for( i = 0; i < ne; i++){
                        if(wc[i].status != IBV_WC_SUCCESS)
                            check_wc_status(wc[i].status);

                        DEBUG_PRINT((stdout, "Completion found in completion queue\n"));
                        ccnt += CQ_MODERATION;
                    }
                }

            } while (ne > 0);

            if( ne < 0 ){
                fprintf(stderr, RED "poll cq\n" RESET);
                return -1;
            }
        }

    }

    free(wc);

    DEBUG_PRINT((stdout, "finishing run_iter\n"));
    return 0;
}

/* QUEUE PAIR STATE MODIFICATION */

static int
modify_qp_to_init(struct ibv_qp *qp)
{
    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    struct ibv_qp_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = config.ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE;

    if( errno = ibv_modify_qp(qp, &attr, flags) ){
        perror("modify_qp()");
        return -1;
    }
    return 0;
}

static int
modify_qp_to_rtr (struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t * dgid)
{
    struct ibv_qp_attr attr;
    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 16;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = config.ib_port;
    if (-1 != config.gid_idx){
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = 1;
        memcpy (&attr.ah_attr.grh.dgid, dgid, 16);
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.sgid_index = config.gid_idx;
        attr.ah_attr.grh.traffic_class = 0;
    }

    if( errno = ibv_modify_qp (qp, &attr, flags) ){
        perror("modify_qp");
        return -1;
    }

    return 0;
}

static int
modify_qp_to_rts (struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    memset (&attr, 0, sizeof (attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 6;
    attr.rnr_retry = 0;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 16; //FIXME hardcoded!

    if( errno = ibv_modify_qp (qp, &attr, flags) ){
        perror("modify_qp");
        return -1;
    }

    return 0;
}

// -1 on error, 0 on success
static int
connect_qp(struct resources *res)
{
    struct cm_con_data_t local_con_data, remote_con_data, tmp_con_data;
    int i;
    char temp_char;
    union ibv_gid my_gid;

    if( -1 != config.gid_idx ){
        if( -1 == ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid) ){
            fprintf(stderr, "gid for port %d, index %d failed\n",
                    config.ib_port, config.gid_idx);
            return -1;
        }
    } else {
        memset(&my_gid, 0, sizeof my_gid);
    }

    for(i=0;i<config.threads;i++){
        DEBUG_PRINT((stdout, "connecting qp for thread %d\n", i));

        char *buf = res->assets[i]->buf;
        struct ibv_pd *pd = res->assets[i]->pd;
        struct ibv_cq *cq = res->assets[i]->cq;
        struct ibv_qp *qp = res->assets[i]->qp;
        struct ibv_mr *mr = res->assets[i]->mr;
        struct cm_con_data_t *remote_props =  &(res->assets[i]->remote_props);

        local_con_data.addr = htonll((uintptr_t) buf);
        local_con_data.rkey = htonl(mr->rkey);
        local_con_data.qp_num = htonl(qp->qp_num);
        local_con_data.lid = htons (res->port_attr.lid);
        memcpy(local_con_data.gid, &my_gid, 16);

        if( 0 > sock_sync_data(res->sock, sizeof(struct cm_con_data_t) , 
                        (char *) &local_con_data, (char *) &tmp_con_data) ){
            fprintf(stderr, RED "failed to exchange conn data\n" RESET);
            return -1;
        }
        remote_con_data.addr = ntohll( tmp_con_data.addr );
        remote_con_data.rkey = ntohl( tmp_con_data.rkey );
        remote_con_data.qp_num = ntohl( tmp_con_data.qp_num );
        remote_con_data.lid = ntohs( tmp_con_data.lid );
        memcpy( remote_con_data.gid, tmp_con_data.gid, 16);
        *remote_props = remote_con_data;

        DEBUG_PRINT((stdout, 
                    "Remote address = 0x%" PRIx64 "\n", remote_con_data.addr));
        DEBUG_PRINT((stdout, 
                    "Remote rkey = 0x%x\n", remote_con_data.rkey));
        DEBUG_PRINT((stdout, 
                    "Remote QP number = 0x%x\n", remote_con_data.qp_num));
        DEBUG_PRINT((stdout, "Remote LID = 0x%x\n", remote_con_data.lid));

        if( config.gid_idx != -1){
            uint8_t *p = remote_con_data.gid;
            DEBUG_PRINT((stdout,
                        "Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x \
                        :%02x:%02x:%02x:%02x:%02x:%02x\n",
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                        p[10], p[11], p[12], p[13], p[14], p[15]));
        }

        if( -1 == modify_qp_to_init(qp) ){
            fprintf(stderr, RED "failed to modify qp to INIT\n" RESET);
            return -1;
        }
        DEBUG_PRINT((stdout, "Modified QP state to INIT\n"));

        if (-1 == modify_qp_to_rtr(qp, remote_con_data.qp_num, remote_con_data.lid,
                remote_con_data.gid)){
            fprintf (stderr, "failed to modify QP state to RTR\n");
            return -1;
        }
        DEBUG_PRINT((stdout, "Modified QP state to RTR\n"));

        if( -1 == modify_qp_to_rts(qp) ){
            fprintf (stderr, "failed to modify QP state to RTR\n");
            return -1;
        }
        DEBUG_PRINT((stdout, "Modified QP state to RTS\n"));

        /* sync to make sure that both sides are in states that they can connect */
        if ( -1 == sock_sync_data(res->sock, 1, "Q", &temp_char) ){
            fprintf (stderr, "sync error after QPs are were moved to RTS\n");
            return -1;
        }
    }

    return 0;
}


/* RESOURCE MANAGEMENT */
static void
resources_init (struct resources *res)                                         
{
    memset (res, 0, sizeof *res);                                              
    res->sock = -1; 
}   

// -1 on error, 0 on success
static int
resources_create (struct resources *res)
{
    struct ibv_device **dev_list=NULL, *ib_dev=NULL;
    struct ibv_qp_init_attr qp_init_attr;
    struct config_t *config_other;
    size_t size;
    int i, j, rc, mr_flags = 0, cq_size = 0, num_devices;

    /* ESTABLISH TCP CONNECTION */
    if (config.server_name) {
        if( 0 > (res->sock = sock_connect(config.server_name, config.tcp_port))) {
            fprintf(stderr, RED "sock_connect\n" RESET);
            return -1;
        }
    } else {
        DEBUG_PRINT((stdout, "waiting on port %d for TCP connection\n", config.tcp_port));
        if( 0 > (res->sock = sock_connect(NULL, config.tcp_port)) ){
            fprintf(stderr, RED "sock_connect\n" RESET);
            return -1;
        }
    }
    DEBUG_PRINT((stdout, "TCP connection was established\n"));


    /* EXCHANGE CONFIG INFO */
    config_other = (struct config_t *) malloc( sizeof(struct config_t) );
    if( 0 > sock_sync_data( res->sock, sizeof(struct config_t), (char *) &config,(char *) config_other) ){
        fprintf(stderr, RED "failed to exchange config data\n" RESET);
        return -1;
    }
    config.xfer_unit =  MAX(config.xfer_unit, config_other->xfer_unit);
    config.iter =       MAX(config.iter, config_other->iter);
    config.threads =    MAX(config.threads, config_other->threads);
    threads = (pthread_t *) malloc( sizeof(pthread_t) * config.threads );
    config.config_other = config_other;
    DEBUG_PRINT((stdout, "buffer %zd bytes, %d iterations on %d threads\n", 
                config.xfer_unit, config.iter, config.threads));


    /* GET IB DEVICES AND SELECT ONE */

    DEBUG_PRINT((stdout, "searching for IB devices in host\n"));

    if( ! (dev_list = ibv_get_device_list(&num_devices)) || ! num_devices ){
        perror("get_device_list (or device not present)");
        return -1;
    }

    DEBUG_PRINT((stdout, "found %d device(s)\n", num_devices));

    for (i = 0; i < num_devices; i++){
        if (!config.dev_name){
            config.dev_name = strdup(ibv_get_device_name (dev_list[i]));
            DEBUG_PRINT((stdout,"device not specified, using first one found: %s\n",config.dev_name));
        }
        if ( !strcmp(ibv_get_device_name(dev_list[i]), config.dev_name) ){
            ib_dev = dev_list[i];
            break;
        }
    }

    ibv_free_device_list(dev_list);

    if( ! (res->ib_ctx = ibv_open_device(ib_dev)) ){
        fprintf(stderr,RED "open_device\n" RESET);
        return -1;
    }

    /* GET LOCAL PORT PROPERTIES */
    if( 0 != (errno = ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr)) ){
        perror("query_port");
        return -1;
    }

    /* ALLOCATE SPACE ALL ASSETS FOR EACH CONNECTION */
    res->assets = (struct ib_assets **) malloc( sizeof(struct ib_assets *) * config.threads);

    for( i = 0; i < config.threads; i++){

        res->assets[i] = (struct ib_assets *) malloc(sizeof(struct ib_assets));


        if( ! (res->assets[i]->pd = ibv_alloc_pd(res->ib_ctx)) ){
            fprintf(stderr, RED "alloc_pd\n" RESET);
            return -1;
        }

        /* CREATE COMPLETION QUEUE */
        if( !(res->assets[i]->cq = ibv_create_cq(res->ib_ctx, CQ_SIZE, NULL,NULL, 0)) ){
            fprintf(stderr, RED "alloc_pd\n" RESET);
            return -1;
        }


        /* CREATE & REGISTER MEMORY BUFFER */
        if( ! (res->assets[i]->buf = (char *) malloc(config.xfer_unit)) ){
            fprintf(stderr, RED "malloc on buf\n" RESET);
            return -1;
        }

        mr_flags = IBV_ACCESS_LOCAL_WRITE | 
            IBV_ACCESS_REMOTE_READ |
            IBV_ACCESS_REMOTE_WRITE;

        if( ! (res->assets[i]->mr = ibv_reg_mr(res->assets[i]->pd, res->assets[i]->buf,
                        config.xfer_unit, mr_flags)) ){
            fprintf(stderr, RED "reg_mr\n" RESET);
            return -1;
        }

        DEBUG_PRINT((stdout,
                    "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
                    res->assets[i]->buf, res->assets[i]->mr->lkey, 
                    res->assets[i]->mr->rkey, mr_flags));
            
        /* CREATE QUEUE PAIR */
        memset(&qp_init_attr, 0, sizeof (qp_init_attr));

        qp_init_attr.qp_type = IBV_QPT_RC;
        //qp_init_attr.sq_sig_all = 0; 
        qp_init_attr.send_cq = res->assets[i]->cq;
        qp_init_attr.recv_cq = res->assets[i]->cq;
        qp_init_attr.cap.max_send_wr = MAX_SEND_WR;
        qp_init_attr.cap.max_recv_wr = MAX_RECV_WR;
        qp_init_attr.cap.max_send_sge = MAX_SEND_SGE;
        qp_init_attr.cap.max_recv_sge = MAX_RECV_SGE;

        if( !(res->assets[i]->qp = ibv_create_qp(res->assets[i]->pd, &qp_init_attr))){
            fprintf(stderr, RED "create_qp\n" RESET); 
            return -1;
        }

        DEBUG_PRINT((stdout, "QP was created, QP number=0x%x\n", res->assets[i]->qp->qp_num));
    }

    return 0;
}

// -1 on error, 0 on success
static int
resources_destroy( struct resources *res )
{
    int i;
    for(i = 0; i < config.threads; i++){
        if( -1 == conn_destroy(res->assets[i]) ){
            fprintf(stderr, RED "conn_destroy failed\n" RESET);
            return -1;
        }
    }

    if( res->ib_ctx && (-1 == ibv_close_device(res->ib_ctx)) ){
        fprintf(stderr, RED "close_device failed\n" RESET);
        return -1;
    }

    if( 0 <= res->sock && (-1 == close(res->sock)) ){
        fprintf(stderr, RED "close failed\n" RESET);
        return -1;
    }
    return 0;
}

// -1 on error, 0 on success
static int
conn_destroy( struct ib_assets *conn )
{
    if (conn->buf)
        free(conn->buf);

    if (conn->qp && (errno = ibv_destroy_qp(conn->qp)) ){
            perror("destroy_qp");
            return -1;
    }

    if (conn->mr && (errno = ibv_dereg_mr(conn->mr)) ){
            perror("dereg_mr");
            return -1;
    }

    if (conn->cq && (errno = ibv_destroy_cq(conn->cq)) ){
            perror("destroy_cq");
            return -1;
    }

    if (conn->pd && (errno = ibv_dealloc_pd (conn->pd)) ){
            perror("dealloc_pd");
            return -1;
    }

    return 0;
}

/* SOCKET OPERATION WRAPPERS */

// -1 on error, 0 on success
static int
sock_connect( const char *servername, int port)
{
    struct addrinfo *resolved_addr = NULL, *iterator;
    char service[6];
    int sockfd = -1, listenfd=0, so_reuseaddr=1, rc;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };
    sprintf(service, "%d", port);

    if( 0 != (rc = getaddrinfo(servername, service, &hints, &resolved_addr)) ) {
        fprintf(stderr, "%s for %s : %d\n", gai_strerror(rc), servername, port);
        return -1;
    }

    for(iterator = resolved_addr; iterator; iterator = iterator->ai_next){
        if(-1 != (sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol))){
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

            if(servername){
                if( -1 == connect(sockfd, iterator->ai_addr, iterator->ai_addrlen) ){
                    perror("connect failure");
                    freeaddrinfo(resolved_addr);
                    close(sockfd);
                    return -1;
                }
            } else {
                listenfd = sockfd;
                sockfd = -1;

                if( -1 == bind(listenfd, iterator->ai_addr, iterator->ai_addrlen) ){
                    perror("bind failure");
                    freeaddrinfo(resolved_addr);
                    close(listenfd);
                    return -1;
                }
                if( -1 == listen(listenfd, 1) ){
                    perror("listen failure");
                    freeaddrinfo(resolved_addr);
                    close(listenfd);
                    return -1;
                }

                if( -1 == (sockfd = accept(listenfd, NULL, 0)) ){
                    perror("accept failure");
                    freeaddrinfo(resolved_addr);
                    close(listenfd);
                    return -1;
                }
            }
        }
    }

    return sockfd;
}

// -1 on error, 0 on success
int
sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
{
    int rc, total_read_bytes = 0;

    if( 0 > (rc = write(sock, local_data, xfer_size))){
        perror("write");
        return -1;
    }

    while( total_read_bytes < xfer_size ){
        if( 0 > (rc = read(sock, remote_data, xfer_size)) ){
            perror("read");
            return -1;
        }
        total_read_bytes += rc;
    }

    return 0;
}

/* UTILITY */
static void
usage (const char *argv0)
{

    fprintf (stderr, "Usage:\n");
    fprintf (stderr, " %s start a server and wait for connection\n", argv0);
    fprintf (stderr, " %s <host> connect to server at <host>\n", argv0);
    fprintf (stderr, "\n");
    fprintf (stderr, "Options:\n");
    fprintf (stderr,
            " -p, --port <port> listen on/connect to port <port> (default 18515)\n");
    fprintf (stderr,
            " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
    fprintf (stderr,
            " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
    fprintf (stderr,
            " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
}

static void
print_config (void)
{
    char *op;
    char *crt;

    fprintf (stdout, YEL "\n\nCONFIG-------------------------------------------\n" );
    fprintf (stdout, "Device name : \"%s\"\n", config.dev_name);
    fprintf (stdout, "IB port : %u\n", config.ib_port);
    if (config.server_name)
        fprintf (stdout, "IP : %s\n", config.server_name);
    fprintf (stdout, "TCP port : %u\n", config.tcp_port);
    if (config.gid_idx >= 0)
        fprintf (stdout, "GID index : %u\n", config.gid_idx);

    opcode_to_str(config.opcode, &op);
    crt_to_str(config.crt, &crt);
    fprintf(stdout, "%s operation requested (for %s test)\n", op, crt);

    if ( !config.iter|| !config.xfer_unit )
        fprintf(stdout, RED "Size of transfer not specified.\n" YEL );
    else
        fprintf(stdout, "%d iters, each %zd bytes\n", config.iter, config.xfer_unit );

    fprintf (stdout, "CONFIG------------------------------------------\n\n" RESET);
}

static void
crt_to_str(int code, char **str)
{
    char *s;
    switch( code ){
        case CRT_BW:
            s = "BANDWIDTH";
            break;
        case CRT_LAT:
            s = "LATENCY";
            break;
        case CRT_CPU:
            s = "CPU USAGE";
            break;
        default:
            s = "ALL";
    }
    *str = malloc( strlen(s) );
    strcpy(*str, s);
    return;
}

static void
opcode_to_str(int opcode, char **str)
{
    char *s;
    switch( opcode ){
        case IBV_WR_RDMA_READ:
            s = "RDMA READ";
            break;
        case IBV_WR_RDMA_WRITE:
            s = "RDMA WRITE";
            break;
        case IBV_WR_SEND:
            s = "SEND";
            break;
        default:
            s = "NONE";
    }
    *str = malloc( strlen(s) );
    strcpy(*str, s);
    return;
}

/*  note on units: bytes/microseconds turns out to be the same as MB/sec
 *
 * */
static void
print_report(unsigned int iters, unsigned size, int duplex,
        int no_cpu_freq_fail)
{
    double xfer_total = config.xfer_unit * config.iter * config.threads;
    long elapsed = ( tcompleted.tv_sec * 1e6 + tcompleted.tv_usec )
        - ( tposted.tv_sec * 1e6 + tposted.tv_usec );
    double avg_bw = xfer_total / elapsed;
    double ucpu;
    double scpu;
    calc_cpu_usage_pct( &pend, &pstart, &ucpu, &scpu);
    
    printf(REPORT_FMT, (int) config.xfer_unit, config.iter, avg_bw, ucpu, scpu);
}

static void
check_wc_status(enum ibv_wc_status status)
{
    switch(status){
        case IBV_WC_SUCCESS:
            fprintf(stderr,"success\n");
            break;
        case IBV_WC_LOC_LEN_ERR:
            fprintf(stderr,"loc len err\n");
            break;
        case IBV_WC_LOC_QP_OP_ERR:
            fprintf(stderr,"loc qp op err\n");
            break;
        case IBV_WC_LOC_EEC_OP_ERR:
            fprintf(stderr,"loc eec op err\n");
            break;
        case IBV_WC_LOC_PROT_ERR:
            fprintf(stderr,"loc prot err\n");
            break;
        case IBV_WC_WR_FLUSH_ERR:
            fprintf(stderr,"wc wr flush err\n");
            break;
        case IBV_WC_MW_BIND_ERR:
            fprintf(stderr,"mw bind err\n");
            break;
        case IBV_WC_BAD_RESP_ERR:
            fprintf(stderr,"bad resp err\n");
            break;
        case IBV_WC_LOC_ACCESS_ERR:
            fprintf(stderr,"loc access err\n");
            break;
        case IBV_WC_REM_INV_REQ_ERR:
            fprintf(stderr,"rem inv req err\n");
            break;
        case IBV_WC_REM_ACCESS_ERR:
            fprintf(stderr,"rem access err\n");
            break;
        case IBV_WC_REM_OP_ERR:
            fprintf(stderr,"rem op err\n");
            break;
        case IBV_WC_RETRY_EXC_ERR:
            fprintf(stderr,"retry exc err\n");
            break;
        case IBV_WC_RNR_RETRY_EXC_ERR:
            fprintf(stderr,"rnr retry exc err\n");
            break;
        case IBV_WC_LOC_RDD_VIOL_ERR:
            fprintf(stderr,"loc rdd viol err\n");
            break;
        case IBV_WC_REM_INV_RD_REQ_ERR:
            fprintf(stderr,"inv rd req\n");
            break;
        case IBV_WC_REM_ABORT_ERR:
            fprintf(stderr,"rem abort\n");
            break;
        case IBV_WC_INV_EECN_ERR:
            fprintf(stderr,"inv eecn\n");
            break;
        case IBV_WC_INV_EEC_STATE_ERR:
            fprintf(stderr,"inv eec state\n");
            break;
        case IBV_WC_FATAL_ERR:
            fprintf(stderr,"fatal err\n");
            break;
        case IBV_WC_RESP_TIMEOUT_ERR:
            fprintf(stderr,"resp timeout err\n");
            break;
        case IBV_WC_GENERAL_ERR:
            fprintf(stderr,"general err\n");
            break;
        default:
            fprintf(stderr, "no matching status code found\n");
            return;
    }
}

static uint16_t 
checksum(void *vdata, size_t length)
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

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t
htonll (uint64_t x)
{
    return bswap_64 (x);
}

static inline uint64_t 
ntohll (uint64_t x)
{
    return bswap_64 (x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN

static inline uint64_t 
htonll (uint64_t x)
{
    return x;
}

static inline uint64_t 
ntohll (uint64_t x)
{
    return x;
}
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif 

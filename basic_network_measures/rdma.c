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
    19876,          /* tcp_port */
    1,              /* ib_port */
    -1,              /* gid_idx */
    0,              /* xfer_unit */
    0,              /* iter */
    -1,             /* opcode */
    1,              /* number of threads */
    0,          /* use events */
    0,          /* length  */
    NULL,
};

struct timeval *tposted; 
struct timeval *tcompleted;

int *iterations;

struct pstat *pstart;
struct pstat *pend;

struct pstat *pstart_server;
struct pstat *pend_server;

int cnt_threads;
int max_cq_handle;

pthread_mutex_t shared_mutex;
pthread_cond_t start_cond;

// TODO really need a better name than this
struct event_polling_t *polling;
//pthread_cond_t *polling_conditions;
//pthread_mutex_t *polling_mutexes;

//cpu_set_t cpuset;
pthread_t *threads;
pthread_t polling_thread;

/* MAIN */
    int
main ( int argc, char *argv[] )
{
    int i, iter, rc = 0;
    uint16_t csum;
    char temp_char;
    void * (* functorun) (void *);
    struct resources res;
    struct timeval cur_time;

    pthread_attr_t attr;
    /*
    CPU_ZERO( &cpuset );
    CPU_SET( CPUNO, &cpuset );
    */
    cnt_threads = 0;

    /* PROCESS CL ARGUMENTS */

    while (1){
        int c;
        static struct option long_options[] = {
            {.name = "port",.has_arg = 1,.val = 'p'},
            {.name = "ib-dev",.has_arg = 1,.val = 'd'},
            {.name = "gid-idx",.has_arg = 1,.val = 'g'},
            {.name = "xfer-unit", .has_arg = 1, .val = 'b'},
            {.name = "iter", .has_arg = 1, .val = 'i'},
            {.name = "verb", .has_arg=1, .val= 'v'},
            {.name = "threads", .has_arg = 1, .val='t'},
            {.name = "event", .has_arg = 1, .val='e'},
            {.name = "length", .has_arg = 1, .val='l'},
            {.name = NULL,.has_arg = 0,.val = '\0'},
        };

        if( (c = getopt_long( argc, argv, "p:d:g:b:i:v:t:e:l:", long_options, NULL)) == -1 ) break;

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
                    printf("gididx\n");
                    usage (argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'b':
                config.xfer_unit = pow(2,strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    printf("xfer\n");
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'i':
                config.iter = strtoul(optarg, NULL, 0);
                if(config.iter < 0){
                    printf("iter\n");
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
                    printf("verb\n");
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 't':
                config.threads = strtoul(optarg, NULL, 0);
                if( config.threads < 0){
                    printf("threads\n");
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'e':
                config.use_event = strtoul(optarg,NULL,0);
                break;
            case 'l':
                config.length = strtoul(optarg, NULL, 0);
                break;
            default:
                printf("default\n");
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

    // FROM HERE ON: functions could contain malloc -----------------------------------

    /* EXCHANGE CONFIGS AND CREATE IB ASSETS */
    if( -1 == resources_create(&res) ){
        fprintf(stderr , RED "resources_create\n" RESET);
        goto main_exit;
    }
    DEBUG_PRINT((stdout,GRN "resources_create() successful\n" RESET));

    /* CONNECT QPs */
    if( -1 == connect_qp(&res) ){
        fprintf(stderr, RED "connect_qp\n" RESET);
        goto main_exit;
    }
    DEBUG_PRINT((stdout, GRN "connect_qp() successful\n" RESET));

   
    /* SET UP FOR MULTITHREAING */
    pthread_mutex_init(&shared_mutex, NULL);
    pthread_cond_init(&start_cond, NULL);
    polling = malloc( sizeof(struct event_polling_t) * (max_cq_handle + 1) );
    for(i=0;i<(max_cq_handle+1);i++) {
        polling[i].semaphore = 0;
        pthread_cond_init(&polling[i].condition, NULL);
        pthread_mutex_init(&polling[i].mutex, NULL);
    }
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);


    // this is a bit convoluted
    // unless this is an IBSR experiment, only client will enter here
    if( config.opcode != -1 ){

        functorun = config.server_name? 
            (void *(*)(void *)) &run_iter_client : (void *(*)(void *)) &run_iter_server;

        for(i=0; i < config.threads; i++){
            res.assets[i]->t_num = i;
            if( errno = pthread_create( &threads[i], &attr, functorun, 
                        (void *) res.assets[i]) ){
                perror("pthread_create");
                goto main_exit;
            }
        }
        DEBUG_PRINT((stdout, GRN "threads created\n" RESET));

        if( config.use_event && 
                (errno = pthread_create(&polling_thread, &attr, 
                                        (void *(*)(void *)) &poll_and_notify, (void *) &res)) ){
                perror("polling thread pthread_create");
                goto main_exit;
            }

        do{
            pthread_mutex_lock( &shared_mutex);
            i = (cnt_threads < config.threads);
            pthread_mutex_unlock( &shared_mutex);
        } while (i);
        DEBUG_PRINT((stdout, GRN "all threads started--signalling start\n" RESET));

        // if we're doing IB-send, client and server are in here,
        // they've spawned their threads, their threads have reached pthread_wait
        // let the client wait for the server to post the RRs
        if (config.opcode == IBV_WR_SEND ){
            if( -1 == sock_sync_data(res.sock, 1, "K", &temp_char ) ){
                fprintf(stderr, RED "IB Send preliminary\n" RESET);
                goto main_exit;
            }
        }

        /* SIGNAL THREADS TO START WORK */

        pthread_cond_broadcast(&start_cond);
        for( i=0; i < config.threads; i++ )
            if( errno = pthread_join(threads[i], NULL) ){
                perror("pthread_join");
                goto main_exit;
            }

        //TODO do something that will tell polling thread to quit || pthread_cancel
        if( config.use_event && (errno = pthread_cancel(polling_thread)) ){
            perror("polling thread pthread_cancel");
            goto main_exit;
        }

        DEBUG_PRINT((stdout, GRN "threads joined--waiting for socket sync\n" RESET));
    }

    /* SERVER BEGINS CPU MEASURE IF USING UNIDIRECTIONAL PROTOCOL (i.e. RDMA R/W)*/
    if( !config.server_name && config.opcode != IBV_WR_SEND){
        get_usage( getpid(), pstart, CPUNO );
    }

    /* SERVER AND CLIENT SYNCS UP HERE AFTER RUNNING EXPERIMENT */
    if( -1 == sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, RED "final sync failed\n" RESET);
        goto main_exit;
    }

    /* SERVER ENDS CPU MEASUREMENT IF USING UNIDIRECTIONAL PROTOCOL (I.E. RDMA R/W)*/
    if( !config.server_name && config.opcode != IBV_WR_SEND ){
        get_usage( getpid(), pend, CPUNO );
    }

    /* EXCHANGE PSTAT DATA */
    if( -1 == sock_sync_data( res.sock, (sizeof(struct pstat) * config.threads), (char *) pstart, (char *) pstart_server)){
        fprintf(stderr, RED "failed to exchange cpu stats\n" RESET);
        goto main_exit;
    }
    if(-1 == sock_sync_data( res.sock, sizeof(struct pstat) * config.threads, (char *) pend, (char *) pend_server)){
        fprintf(stderr, RED "failed to exchange cpu stats\n" RESET);
        goto main_exit;
    }

    DEBUG_PRINT((stdout, GRN "final socket sync finished--terminating\n" RESET));

    rc = 1; // signifies that everything was run without errors
main_exit:
    if( -1 == resources_destroy(&res) )
        fprintf(stderr, RED "resources_destroy\n" RESET);

    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&shared_mutex);
    pthread_cond_destroy(&start_cond);

    if (config.dev_name) free ((char *) config.dev_name);
    if (config.config_other) free((char *) config.config_other);
    if (threads) free((char *) threads);

    /* REPORT ON EXPERIMENT TO STDOUT */

    if(rc){
        print_report();
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

/* THREAD FUNCTIONS */
    static int
run_iter_client(void *param)
{
    /* DECLARE AND INITIALIZE */
    struct ib_assets *conn = (struct ib_assets *) param;
    int final = 0,rc, scnt=0, ccnt=0, ne, i, signaled=1, cq_handle = conn->cq->handle;
    int t_num = conn->t_num;
    long int tposted_us;
    long int elapsed;
    uint16_t csum;
    struct timeval tnow;
    pthread_t thread = pthread_self();
    pthread_cond_t *my_cond; 
    pthread_mutex_t *my_mutex; 

    /* THREAD-SAFE INITIALIZATIONS */
    pthread_mutex_lock( &shared_mutex);
    int use_event = config.use_event, opcode = config.opcode, xfer_unit = config.xfer_unit, 
        iter = config.iter, length = config.length;
    struct timeval *mytposted =  &(tposted[t_num]);
    struct timeval *mytcompleted =  &(tcompleted[t_num]);

    struct pstat *mypstart = &(pstart[t_num]);
    struct pstat *mypend = &(pend[t_num]);
    if( use_event ){ 
        my_cond = &(polling[cq_handle].condition);
        my_mutex = &(polling[cq_handle].mutex);
    }
    pthread_mutex_unlock( &shared_mutex);

    DEBUG_PRINT((stdout, "[thread %u] spawned, handle #%d \n", (unsigned int) thread, cq_handle));

    /* SET UP WORK REQUEST AND SCATTER-GATHER ENTRY */
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t) conn->buf;
    sge.lkey = conn->mr->lkey;
    sge.length = xfer_unit;
    memset(&sr, 0, sizeof(sr));
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = opcode;
    sr.next = NULL;
    sr.send_flags |= IBV_SEND_SIGNALED;
    sr.wr_id = 0;
    if( opcode != IBV_WR_SEND ){
        sr.wr.rdma.remote_addr = conn->remote_props.addr;
        sr.wr.rdma.rkey = conn->remote_props.rkey;
    }
    struct ibv_wc *wc;
    struct ibv_send_wr *bad_wr=NULL;
#ifdef NUMA
    wc = (struct ibv_wc *) numa_alloc_local(sizeof(struct ibv_wc));
#else
    wc = (struct ibv_wc *) malloc(sizeof(struct ibv_wc));
#endif

    /* PIN THREAD TO RESPECTIVE CPU */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(t_num, &cpuset);
    if( errno = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) ){
        perror("pthread_setaffinity");
        return -1;
    }

    /* WAIT TO SYNCHRONIZE */
    pthread_mutex_lock( &shared_mutex);

    cnt_threads++; // tell main thread that this thread is ready
    pthread_cond_wait( &start_cond, &shared_mutex); // wait until all threads are ready

    /* INITIAL MEASUREMENT */
    gettimeofday( mytposted, NULL ); // TODO should be called on tposted[t_num]
    get_usage( getpid(), mypstart, t_num);
    tposted_us = mytposted->tv_sec * 1e6 + mytposted->tv_usec;

    pthread_mutex_unlock( &shared_mutex);

    DEBUG_PRINT((stdout, "[thread %u] starting\n", (unsigned int) thread));

    while(1) {
        
        while( ( scnt -ccnt ) < CQ_MODERATION && (length || scnt < iter) ){
            if( scnt % CQ_MODERATION == 0){
                sr.send_flags &= ~IBV_SEND_SIGNALED;
                signaled = 0;
            }
            sr.wr_id = scnt;
            DEBUG_PRINT((stdout, "[wr_id %d, signaled? %d]\n", scnt, signaled));
#ifdef DEBUG
            csum = checksum(conn->buf, xfer_unit);
            fprintf(stdout, WHT "\tchecksum: %0x\n" RESET, csum);
#endif
            if( final && scnt % CQ_MODERATION - 1 )
                memset(conn->buf, 1, 1);

            if( ( errno = ibv_post_send(conn->qp, &sr, &bad_wr) ) ){
                fprintf(stdout, RED "scnt - ccnt = %d\n" RESET,(scnt - ccnt));
                perror("post_send");
                return -1;
            }
            ++scnt;
            if( (scnt % CQ_MODERATION == CQ_MODERATION - 1) ||
                    (!length && scnt == iter - 1) ){
                sr.send_flags |= IBV_SEND_SIGNALED;
                signaled = 1;
            }
        }

        if( use_event ){
            DEBUG_PRINT((stdout, "[thread %u] about to wait on my condition\n",(unsigned int)thread));
            pthread_mutex_lock( my_mutex );
            DEBUG_PRINT((stdout, "[thread %u] acquired lock\n",(unsigned int)thread));
            polling[cq_handle].semaphore++;
            pthread_cond_wait( my_cond, my_mutex );
            pthread_mutex_unlock( my_mutex );
            DEBUG_PRINT((stdout, "[thread %u] released from cond_wait\n", (unsigned int )thread));
        }

        // retrieve completion and add to ccnt
        if( ccnt < scnt ){
            do {
                ne = ibv_poll_cq(conn->cq, 1, wc);
                if( ne > 0 ){
                    for(i = 0; i < ne; i++){
                        DEBUG_PRINT((stdout, GRN"[POLL RETURNED]----------\n"RESET));
                        DEBUG_PRINT((stdout, "%d requests on wire (max %d allowed)\n", 
                                    (scnt - ccnt), CQ_MODERATION));
                        if( wc[i].status != IBV_WC_SUCCESS){
                            check_wc_status(wc[i].status);
                            fprintf(stderr, "Completion with error. wr_id: %lu\n", wc[i].wr_id);
                            return -1;
                        } else {
                            DEBUG_PRINT((stdout, "Completion success: wr_id: %lu ccnt: %d\n", 
                                        wc[i].wr_id, ccnt));
                            ccnt += CQ_MODERATION;
                        }
                    }
                }
            } while ( ne > 0 );
        }

        if( ne < 0 ){
            fprintf(stderr, RED "poll cq\n" RESET);
            return -1;
        }

        if(final){
            pthread_mutex_lock( &shared_mutex );
            if( length ) { // if experiment length is specified, config.iter will be the total number
                            // of iterations (otherwise, it's the per-thread iteration)
                config.iter += scnt;
                iterations[t_num] = scnt;
            }
            gettimeofday(mytcompleted, NULL);
            get_usage( getpid(), mypend, t_num);
            pthread_mutex_unlock(&shared_mutex);

            DEBUG_PRINT((stdout, "[thread %u ]finishing\n", (unsigned int)thread));
            break;
        }

        if( use_event && (errno = ibv_req_notify_cq(conn->cq, 0))){
            perror("ibv_post_recv");
            return -1;
        }

        gettimeofday( &tnow, NULL);
        elapsed = (tnow.tv_sec * 1e6 + tnow.tv_usec) - tposted_us;

        // completion accounted for every request, experiment either long enough or exhausted iter
        if( scnt == ccnt && ( length && (elapsed > (length * 1e6)) || 
                  ( !length && scnt >= iter - CQ_MODERATION && 
                    ccnt >= iter - CQ_MODERATION) ) ){
            DEBUG_PRINT((stdout, "final batch\n"));
            final = 1;
        }
    }


#ifdef NUMA
    numa_free(wc, sizeof(struct ibv_wc));
#else
    free(wc);
#endif

    DEBUG_PRINT((stdout, "finishing run_iter\n"));
    return 0;
}


// RUN BY SERVER ONLY IF OPERATION IS IBSR 
    static int
run_iter_server(void *param)
{
    /* DECLARE AND INITIALIZE */
    pthread_t thread = pthread_self();
    DEBUG_PRINT((stdout, "[thread %u] spawned\n", (int) thread));
    struct ib_assets *conn = (struct ib_assets *) param;
    int rcnt = 0, ccnt = 0, cq_handle = conn->cq->handle;
    int ne, i, initial_recv_count, t_num = conn->t_num;
    uint16_t csum;
    pthread_cond_t *my_cond; 
    pthread_mutex_t *my_mutex; 

    /* THREAD-SAFE INITIALIZATIONS */
    pthread_mutex_lock(&shared_mutex);

    struct timeval *mytposted = &(tposted[t_num]);
    struct timeval *mytcompleted = &(tcompleted[t_num]);

    struct pstat *mypstart = &pstart[t_num];
    struct pstat *mypend = &pend[t_num];
    int use_event = config.use_event, opcode = config.opcode, 
        xfer_unit = config.xfer_unit, iter = config.iter, length = config.length;
    if( use_event ){
        my_cond = &(polling[cq_handle].condition);
        my_mutex = &(polling[cq_handle].mutex);
    }

    pthread_mutex_unlock(&shared_mutex);

    /* SET UP WORK REQUEST */
    struct ibv_sge sge; 
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t) conn->buf;
    sge.lkey = conn->mr->lkey;
    sge.length = xfer_unit;
    struct ibv_recv_wr rr;
    memset(&rr, 0, sizeof(rr));
    rr.sg_list = &sge;
    rr.num_sge = 1;
    rr.next = NULL;
    rr.wr_id = 0;
    struct ibv_wc *wc;
    struct ibv_recv_wr *bad_wr = NULL;
#ifdef NUMA
    wc = (struct ibv_wc *) numa_alloc_local(sizeof(struct ibv_wc));
#else
    wc = (struct ibv_wc *) malloc(sizeof(struct ibv_wc));
#endif
    

    /* PIN THREAD TO RESPECTIVE CPU */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if( errno = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) ){
        perror("pthread_setaffinity");
        return -1;
    }

    DEBUG_PRINT((stdout, "[thread %u] posting initial recv WR\n", (int) thread));

    //FIXME this might leave us with uncompleted RRs, but for now we're not concerned
    initial_recv_count = iter? MIN(MAX_RECV_WR, iter) : MAX_RECV_WR;
    DEBUG_PRINT((stdout, "number of initial RRs to be posted: %d\n", initial_recv_count));
    for(i = 0; i < initial_recv_count ; i++){
        rr.wr_id = rcnt;
        rcnt++;
        if( errno = ibv_post_recv(conn->qp, &rr, &bad_wr) ){
            fprintf(stderr, "%d-th post\n", i);
            perror("ibv_post_recv");
            return -1;
        }
    }

    /* SYNC-UP POINT FOR ALL WORKER THREADS */
    pthread_mutex_lock( &shared_mutex);
    cnt_threads++;
    pthread_cond_wait( &start_cond, &shared_mutex);
    gettimeofday( mytposted, NULL );
    get_usage( getpid(), mypstart, t_num );
    pthread_mutex_unlock( &shared_mutex);

    DEBUG_PRINT((stdout, "[thread %u] starting\n", (int) thread));

    while( length || (iter && ccnt < iter) ){
        if( use_event ){
            DEBUG_PRINT((stdout, "[thread %u] about to wait on my condition\n",(unsigned int)thread));
            pthread_mutex_lock(my_mutex);
            DEBUG_PRINT((stdout, "[thread %u] acquired lock\n",(unsigned int)thread));
            polling[cq_handle].semaphore++;
            pthread_cond_wait( my_cond, my_mutex );
            pthread_mutex_unlock(my_mutex);
            DEBUG_PRINT((stdout, "[thread %u] released from cond_wait\n", (unsigned int )thread));
        }

        do {
            ne = ibv_poll_cq(conn->cq, WC_SIZE, wc);

            if(ne > 0){
                for(i = 0; i < ne ; i++){
                    if( wc[i].status != IBV_WC_SUCCESS ){
                        check_wc_status(wc[i].status);
                        DEBUG_PRINT((stdout, "Completion with error. wr_id: %lu\n", wc[i].wr_id));
                        return -1;
                    } else {
                        ccnt++;
                        DEBUG_PRINT((stdout, "Completion success: wr_id: %lu, ccnt: %d \
                                    , number of RRs on RQ: %d\n", wc[i].wr_id, ccnt,(rcnt - ccnt)));
#ifdef DEBUG
                        csum = checksum(conn->buf, xfer_unit);
                        DEBUG_PRINT((stdout, WHT "\tchecksum of buffer received: %0x\n" RESET, csum));
#endif

                        if( length || (!length && rcnt < iter) ){
                            rr.wr_id = rcnt;
                            if( errno = ibv_post_recv(conn->qp, &rr, &bad_wr) ){
                                perror("ibv_post_recv");
                                return -1;
                            }
                            DEBUG_PRINT((stdout, "posted new RR. wr_id = %d\n", rcnt));
                            rcnt++;
                        }
                    }
                }
            }
        } while( ne > 0 );

        if( ne < 0 ){
            fprintf(stderr, RED "poll cq\n" RESET);
            return -1;
        }

        if( conn->buf[0] ){
            DEBUG_PRINT((stdout, "final iteration recognized #%d\n",rcnt));
            if(length) iter += rcnt;
            break;
        }

        if( use_event )
            ibv_req_notify_cq(conn->cq, 0);
    }

    pthread_mutex_lock( &shared_mutex );
    gettimeofday( mytcompleted, NULL );
    get_usage( getpid(), mypend, t_num );
    pthread_mutex_unlock( &shared_mutex );

#ifdef NUMA
    numa_free(wc, sizeof(struct ibv_wc));
#else
    free(wc);
#endif

    DEBUG_PRINT((stdout, "finishing run_iter\n"));
    return 0;
}

static void
poll_and_notify(void *param)
{
    pthread_t thread = pthread_self();
    DEBUG_PRINT((stdout, "[thread %u] polling thread here\n", (unsigned int)thread));

    struct resources *res = (struct resources *) param;
    struct ibv_cq *ev_cq;
    void *ev_ctx;


    while(1){
        DEBUG_PRINT((stdout, "[thread %u] about to call get_cq_evnet \n", (unsigned int)thread));
        if( ibv_get_cq_event(res->channel, &ev_cq, &ev_ctx) ){
            fprintf(stderr, RED "ibv_get_cq_event failed\n" RESET);
            return;
        }
        DEBUG_PRINT((stdout, "[thread %u] event recieved\n", (unsigned int)thread));
     
        //DO WE NEED TO CALL THE LOCK?
        while(1){
            if(polling[ev_cq->handle].semaphore){
                pthread_mutex_lock(&polling[ev_cq->handle].mutex);
                if( (errno = pthread_cond_signal(&(polling[ev_cq->handle].condition))) ){
                    fprintf(stderr, RED "pthread_cond_signal failed\n" RESET);
                    return;
                }
                polling[ev_cq->handle].semaphore--;
                pthread_mutex_unlock(&polling[ev_cq->handle].mutex);
                break;
            } 
        }

        DEBUG_PRINT((stdout, "[thread %u] relevant worker thread (handle: %d) notified\n", (unsigned int) thread, ev_cq->handle ));
        
        ibv_ack_cq_events( ev_cq, 1 );

        DEBUG_PRINT((stdout, "[thread %u] event acked\n", (unsigned int)thread));
    }
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
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
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
    int i, j, rc, mr_flags = 0, cq_size, num_devices;
    max_cq_handle = 0;


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
    if( -1 == sock_sync_data( res->sock, sizeof(struct config_t), (char *) &config, 
                (char *) config_other) ){
        fprintf(stderr, RED "failed to exchange config data\n" RESET);
        return -1;
    }
    config.xfer_unit =  MAX(config.xfer_unit, config_other->xfer_unit);
    config.iter =       MAX(config.iter, config_other->iter);
    config.length =     MAX(config.length, config_other->length);
    config.threads =    MAX(config.threads, config_other->threads);
    config.use_event = config.use_event || config_other->use_event;
    threads = (pthread_t *) malloc( sizeof(pthread_t) * config.threads );
    pstart =  (struct pstat *) malloc(sizeof(struct pstat) * config.threads);
    pend =  (struct pstat *) malloc(sizeof(struct pstat) * config.threads);

    iterations = (int *) malloc(sizeof(int) * config.threads);

    tposted = (struct timeval *) malloc(sizeof(struct timeval) * config.threads);
    tcompleted = (struct timeval *) malloc(sizeof(struct timeval) * config.threads);

    pstart_server =  (struct pstat *) malloc(sizeof(struct pstat) * config.threads);
    pend_server =  (struct pstat *) malloc(sizeof(struct pstat) * config.threads);
    config.config_other = config_other;

    if( config.server_name ){
        cq_size = MAX_SEND_WR;
    } else {
        cq_size = MAX_RECV_WR;
    }
    DEBUG_PRINT((stdout, "cq size: %d\n", cq_size));

    if( !config.server_name && config_other->opcode == IBV_WR_SEND )
        config.opcode = IBV_WR_SEND;

    DEBUG_PRINT((stdout, "buffer %zd bytes, %ld iterations on %d threads\n", 
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

    /* SET UP COMPLETION EVENT CHANNEL */
    if(config.use_event){
        res->channel = ibv_create_comp_channel(res->ib_ctx);
        if( ! res->channel ){
            fprintf(stderr,RED "open_device\n" RESET);
            return -1;
        }
    }

    /* GET LOCAL PORT PROPERTIES */
    if( 0 != (errno = ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr)) ){
        perror("query_port");
        return -1;
    }

    /* ALLOCATE SPACE ALL ASSETS FOR EACH CONNECTION */

#ifdef NUMA
    res->assets = (struct ib_assets **) numa_alloc_local( sizeof(struct ib_assets *) * config.threads);
#else
    res->assets = (struct ib_assets **) malloc( sizeof(struct ib_assets *) * config.threads);
#endif

    for( i = 0; i < config.threads; i++){

#ifdef NUMA
        res->assets[i] = (struct ib_assets *) numa_alloc_local(sizeof(struct ib_assets));
#else
        res->assets[i] = (struct ib_assets *) malloc(sizeof(struct ib_assets));
#endif

        if( ! (res->assets[i]->pd = ibv_alloc_pd(res->ib_ctx)) ){
            fprintf(stderr, RED "alloc_pd\n" RESET);
            return -1;
        }

        /* CREATE COMPLETION QUEUE */
        if( !(res->assets[i]->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, res->channel, 0)) ){
            fprintf(stderr, RED "alloc_pd\n" RESET);
            return -1;
        }

        if( res->assets[i]->cq->handle > max_cq_handle ){
            max_cq_handle = res->assets[i]->cq->handle;
        }
    
        if( config.use_event ){
            if(ibv_req_notify_cq(res->assets[i]->cq, 0)){
                fprintf(stderr, RED "ibv_req_notify_cq\n" RESET);
                return -1;
            }
        }


        /* CREATE & REGISTER MEMORY BUFFER */
#ifdef NUMA
        if( ! (res->assets[i]->buf = (char *) numa_alloc_local(config.xfer_unit)) ){
#else
        if( ! (res->assets[i]->buf = (char *) malloc(config.xfer_unit)) ){
#endif
            fprintf(stderr, RED "malloc on buf\n" RESET);
            return -1;
        }

        memset(res->assets[i]->buf, 0, config.xfer_unit);

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

        // FIXME we've been doing RC all along
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.sq_sig_all = 0; 
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

    DEBUG_PRINT((stdout, "max cq handle: %u\n", max_cq_handle));

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

#ifdef NUMA
    numa_free(res->assets, sizeof(struct ib_assets *) * config.threads);
#else
    free(res->assets);
#endif

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
    if (conn->buf){
#ifdef NUMA
        numa_free(conn->buf, config.xfer_unit);
#else
        free(conn->buf);
#endif
    }

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

#ifdef NUMA
    numa_free(conn, sizeof(struct ib_assets) );
#else
    free(conn);
#endif
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

    fprintf (stdout, YEL "\n\nCONFIG-------------------------------------------\n" );
    fprintf (stdout, "Device name : \"%s\"\n", config.dev_name);
    fprintf (stdout, "IB port : %u\n", config.ib_port);
    if (config.server_name)
        fprintf (stdout, "IP : %s\n", config.server_name);
    fprintf (stdout, "TCP port : %u\n", config.tcp_port);
    if (config.gid_idx >= 0)
        fprintf (stdout, "GID index : %u\n", config.gid_idx);

    opcode_to_str(config.opcode, &op);
    fprintf(stdout, "%s operation requested\n", op);

    if ( !config.iter|| !config.xfer_unit )
        fprintf(stdout, RED "transfer spec not specified.\n" YEL );
    else
        fprintf(stdout, "%ld iters, each %zd bytes, %d threads\n", 
                config.iter, config.xfer_unit, config.threads );

    fprintf (stdout, "CONFIG------------------------------------------\n\n" RESET);
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
print_report()
{
    double xfer_total, elapsed, *avg_bw, *avg_lat;
    double *ucpu, *scpu, *ucpu_server, *scpu_server;
    int total_iterations;
    int power = log(config.xfer_unit) / log(2), i;

    struct stats ucpu_stats;
    struct stats scpu_stats;
    struct stats bw_stats;
    struct stats lat_stats;

    struct pstat start_usage;
    struct pstat end_usage;

    ucpu = malloc(sizeof(double) * config.threads);
    scpu = malloc(sizeof(double) * config.threads);
    avg_bw = malloc(sizeof(double) * config.threads);
    avg_lat = malloc(sizeof(double) * config.threads);
    ucpu_server = malloc(sizeof(double) * config.threads);
    scpu_server = malloc(sizeof(double) * config.threads);

    /* COMPUTE CPU USAGE FOR EACH THREAD */
    if(config.threads == 1){
        /* ONE THREAD */
        xfer_total = config.xfer_unit * config.iter;

        elapsed = (tcompleted->tv_sec * 1e6 + tcompleted->tv_usec) -
            (tposted->tv_sec * 1e6 + tposted->tv_usec);
        avg_bw[0] = (double) xfer_total / elapsed;
        avg_lat[0] = elapsed / (double) config.iter;

        if(pend->cpu_total_time - pstart->cpu_total_time)
            calc_cpu_usage_pct( pend, pstart, ucpu, scpu );
        if(pend_server->cpu_total_time - pstart_server->cpu_total_time)
            calc_cpu_usage_pct( pend_server, pstart_server, ucpu_server, scpu_server);

        printf(REPORT_FMT, config.threads, power, config.iter, 
                avg_bw, avg_lat, ucpu[0], scpu[0], ucpu_server[0], scpu_server[0]);
    } else {
        /* MULTIPLE THREADS */

        for(i=0; i < config.threads; i++) {
            if ( pend[i].cpu_total_time - pstart[i].cpu_total_time )
                calc_cpu_usage_pct(&(pend[i]), &(pstart[i]), &(ucpu[i]), &(scpu[i]));

            if (config.length)
                xfer_total = config.xfer_unit * iterations[i];
            else
                xfer_total = config.xfer_unit * config.iter;

            elapsed = (tcompleted[i].tv_sec * 1e6 + tcompleted[i].tv_usec) -
                (tposted[i].tv_sec * 1e6 + tposted[i].tv_usec);
            avg_bw[i] = xfer_total / elapsed;
            avg_lat[i] = elapsed / iterations[i];
        }

        get_stats(avg_bw, config.threads, &bw_stats);
        get_stats(avg_lat, config.threads, &lat_stats);
        get_stats(ucpu, config.threads, &ucpu_stats);
        get_stats(scpu, config.threads, &scpu_stats);

        /*
        printf("[bw] min: %f max: %f average: %f median: %f\n", 
                bw_stats.min, bw_stats.max, bw_stats.average, bw_stats.median);
        printf("[lat] min: %f max: %f average: %f median: %f\n", 
                lat_stats.min, lat_stats.max, lat_stats.average, lat_stats.median);
        printf("[ucpu] min: %f max: %f average: %f median: %f\n", 
                ucpu_stats.min, ucpu_stats.max, ucpu_stats.average, ucpu_stats.median);
        printf("[scpu] min: %f max: %f average: %f median: %f\n",
                scpu_stats.min, scpu_stats.max, scpu_stats.average, scpu_stats.median);

        printf(MTHREAD_FMT, config.threads, config,xfer_size, config.iterations
                bw_stats.average, bw_stats.max, bw_stats.min, bw_stats.median,
                lat_stats.average, lat_stats.max, lat_stats.min, lat_stats.median,
                ucpu_stats.average, ucpu_stats.max, ucpu_stats.min, ucpu_stats.median,
                scpu_stats.average, scpu_stats.max, scpu_stats.min, scpu_stats.median);
                */

        // threads, buffer size, iterations
        // bw avg, bw maximum, bw minimum
        // lat avg, lat maximum, lat minimum 
        // scpu avg, scpu maximum, scpu minimum, 
        // ucpu avg, ucpu maximum, ucpu minimum
    }

    free(ucpu);
    free(scpu);
    free(avg_bw);
    free(avg_lat);
    free(ucpu_server);
    free(scpu_server);

    // format: threads, transfer unit, iterations, avg_bw, avg_lat, ucpu,scpu,ucpuS,scpuS
}


    static int
compare_doubles(const void *a, const void *b){
    double diff = *(double *) a - *(double *) b;
    if (diff > 0.) return 1;
    if (diff < 0.) return -1;
}

    static void
get_stats(double *data, int size, struct stats *stats)
{
    double max = 0., min = DBL_MAX, average = 0., median;
    int i;

    qsort(data, size, sizeof(double), compare_doubles);
    if( size % 2 ) {
        median = data[(int) floor(size/2)];
    } else {
        median = (data[(size/2)] + data[(size/2)-1]) / 2;
    }

    for(i=0; i < size; i++){
        double val = data[i];
        printf("%d: %f, ", i, val);
        if( val > max )
            max = val;
        if( val < min )
            min = val;
        average += val;
    }
    printf("\n");

    stats->max = max;
    stats->min = min;
    stats->average = (average / (double) size);
    stats->median = median;
    return;
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

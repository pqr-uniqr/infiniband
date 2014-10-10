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
    CRT_ALL,       /* criteria */
    NULL,
};

cycles_t *tposted; 
cycles_t *tcompleted; 

/* MAIN */
int main ( int argc, char *argv[] )
{
    int rc = 1;
    int i;
    int iter;
    uint16_t csum;
    struct resources res;
    char temp_char;
    struct timeval cur_time;
    long start_time_usec;
    long cur_time_usec;
    long d;
    struct metrics_t met;

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
            {.name = NULL,.has_arg = 0,.val = '\0'}
        };

        if( (c = getopt_long(argc,argv, "p:d:g:b:i:v:c:", long_options, NULL)) == -1 ) break;

        switch (c)
        {
            case 'p':
                config.tcp_port = strtoul (optarg, NULL, 0);
                break;
            case 'd':
                config.dev_name = strdup (optarg);
                break;
/*             case 'i':
 *                 config.ib_port = strtoul (optarg, NULL, 0);
 *                 if (config.ib_port < 0)
 *                 {
 *                     usage (argv[0]);
 *                     return 1;
 *                 }
 *                 break;
 */
            case 'g':
                config.gid_idx = strtoul (optarg, NULL, 0);
                if (config.gid_idx < 0)
                {
                    usage (argv[0]);
                    return 1;
                }
                break;
            case 'b':
                config.xfer_unit = pow(2,strtoul(optarg,NULL,0));
                if(config.xfer_unit < 0){
                    usage(argv[0]);
                    return 1;
                }
                break;
            case 'i':
                config.iter = strtoul(optarg, NULL, 0);
                if(config.iter < 0){
                    usage(argv[0]);
                    return 1;
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
                    return 1;
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
                    config.crt = CRT_ALL;
                }
                break;
            default:
                usage (argv[0]);
                return 1;
        }
    }

    /* PARSE SERVER NAME IF GIVEN*/
    if (optind == argc - 1){
        config.server_name = argv[optind];
    } else if (optind < argc){
        usage (argv[0]);
        return 1;
    }

    /* SUM UP CONFIG */
#ifdef DEBUG
    print_config();
#endif

    /* INITIATE RESOURCES  */
    resources_init(&res);
    DEBUG_PRINT((stdout, GRN "resources_init() successful\n" RESET));

    /* SET UP RESOURCES  */
    if( resources_create(&res) ){
        fprintf(stderr, RED "resources_create() failed\n" RESET);
        rc = 1;
        goto main_exit;
    }
    DEBUG_PRINT((stdout,GRN "resources_create() successful\n" RESET));

    /* CONNECT QUEUE PAIRS */
    if( connect_qp(&res) ){
        fprintf(stderr, RED "connect_qp() failed\n" RESET);
        rc = 1;
        goto main_exit;
    }
    DEBUG_PRINT((stdout, GRN "connect_qp() successful\n" RESET));
    rc = 0;

    ALLOCATE(tposted, cycles_t, config.iter);
    ALLOCATE(tcompleted, cycles_t, config.iter);

    /* START ITERATIONS! */
    if(config.opcode != -1)
        run_iter(&res);

    if( sock_sync_data(res.sock, 1, "R", &temp_char ) ){
        fprintf(stderr, "sync error while in data transfer\n");
        return 1;
    }

    DEBUG_PRINT((stdout, GRN "final socket sync finished--terminating\n" RESET));

main_exit:
    if (resources_destroy (&res)){
        fprintf (stderr, "failed to destroy resources\n");
        rc = 1;
    }
    if (config.dev_name) free ((char *) config.dev_name);
    if (config.config_other) free((char *)config.config_other);

    /* REPORT ON EXPERIMENT TO STDOUT */
    //report_result( met ); FIXME 
    
    print_report(config.iter, config.xfer_unit, 0, 0);
    
    free(tposted);
    free(tcompleted);
    
    return rc;
}

/*  */
static int run_iter(struct resources *res)
{
    DEBUG_PRINT((stdout,"run_iter called\n"));

    char temp_char;
    int scnt = 0;
    int ccnt = 0;
    int rc = 0;
    int ne;
    int i;

    struct ibv_send_wr sr;
    struct ibv_sge sge; //FIXME do we need to do scatter/gather? (what is it anyways?)
    struct ibv_send_wr *bad_wr = NULL;
    struct ibv_wc *wc = NULL;

    ALLOCATE(wc, struct ibv_wc, 1);

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL; 
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = config.opcode;

    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t) res->buf;
    sge.length = config.xfer_unit;
    sge.lkey = res->mr->lkey;


    if( config.opcode != IBV_WR_SEND ){
        sr.wr.rdma.remote_addr = res->remote_props.addr;
        sr.wr.rdma.rkey = res->remote_props.rkey;
    }

    while( scnt < config.iter || ccnt < config.iter ){

        //TODO tx_depth hardcoded for now
        while( scnt < config.iter && (scnt - ccnt) < MAX_SEND_WR ){

            if((scnt % CQ_MODERATION) == 0)
                sr.send_flags &= ~IBV_SEND_SIGNALED;
       
            tposted[scnt] = get_cycles();
            if( ( rc = ibv_post_send(res->qp, &sr, &bad_wr) ) ){
                perror("ibv_post_send");
                fprintf(stderr, "Couldn't post send: scnt=%d\n", scnt);
                return 1;
            }
            ++scnt;
            //DEBUG_PRINT((stdout, "Work request posted\n"));

            if( scnt % CQ_MODERATION == CQ_MODERATION -1 || scnt == config.iter - 1 )
                sr.send_flags |= IBV_SEND_SIGNALED;
        }

        if( ccnt < config.iter ){
            do {
                ne = ibv_poll_cq(res->cq, 1, wc);
                if( ne > 0 ){
                    for( i = 0; i < ne; i++){
                        if(wc[i].status != IBV_WC_SUCCESS)
                            check_wc_status(wc[i].status);

                        DEBUG_PRINT((stdout, "Completion found in completion queue\n"));
                        ccnt += CQ_MODERATION;
                        
                        if(ccnt >= config.iter -1)
                            tcompleted[config.iter-1] = get_cycles();
                        else
                            tcompleted[ccnt-1] = get_cycles();
                    }
                }

            } while (ne > 0);

            if( ne < 0){
                fprintf(stderr, "poll CQ failed %d\n", ne);
                return 1;
            }
        }

    }

    free(wc);
    return 0;
}


/* IB OPERATIONS */
static int post_send (struct resources *res, int opcode)
{
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_wr = NULL;
    int rc;
    /* prepare the scatter/gather entry */
    memset (&sge, 0, sizeof (sge));
    sge.addr = (uintptr_t) res->buf;
    sge.length = config.xfer_unit;
    sge.lkey = res->mr->lkey;

    /* prepare the send work request */
    memset (&sr, 0, sizeof (sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = opcode;
    sr.send_flags = IBV_SEND_SIGNALED;
    if (opcode != IBV_WR_SEND)
    {
        sr.wr.rdma.remote_addr = res->remote_props.addr;
        sr.wr.rdma.rkey = res->remote_props.rkey;
    }
    /* there is a Receive Request in the responder side, so we won't get any into RNR flow */
    rc = ibv_post_send (res->qp, &sr, &bad_wr);
    if (rc)
        fprintf (stderr, "failed to post SR\n");
    else
    {
        switch (opcode)
        {
            case IBV_WR_SEND:
                DEBUG_PRINT((stdout, "Send Request was posted\n"));
                break;
            case IBV_WR_RDMA_READ:
                DEBUG_PRINT((stdout, "RDMA Read Request was posted\n"));
                break;
            case IBV_WR_RDMA_WRITE:
                DEBUG_PRINT((stdout, "RDMA Write Request was posted\n"));
                break;
            default:
                DEBUG_PRINT((stdout, "Unknown Request was posted\n"));
                break;
        }
    }
    return rc;
}
static int poll_completion (struct resources *res)
{
    struct ibv_wc wc;
    unsigned long start_time_msec;
    unsigned long cur_time_msec;
    struct timeval cur_time;
    int poll_result;
    int rc = 0;
    /* poll the completion for a while before giving up of doing it .. */
    gettimeofday (&cur_time, NULL);
    start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
    do
    {
        poll_result = ibv_poll_cq (res->cq, 1, &wc);
        gettimeofday (&cur_time, NULL);
        cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
    }
    while ((poll_result == 0)
            && ((cur_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));
    if (poll_result < 0)
    {
        /* poll CQ failed */
        fprintf (stderr, "poll CQ failed\n");
        rc = 1;
    }
    else if (poll_result == 0)
    {
        /* the CQ is empty */
        fprintf (stderr, "completion wasn't found in the CQ after timeout\n");
        rc = 1;
    }
    else
    {
        /* CQE found */
        DEBUG_PRINT((stdout, "completion was found in CQ with status 0x%x\n", wc.status));
#ifdef DEBUG
        check_wc_status(wc.status);
#endif

        /* check the completion status (here we don't care about the completion opcode */
        if (wc.status != IBV_WC_SUCCESS)
        {

            fprintf (stderr,
                    "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n",
                    wc.status, wc.vendor_err);
            rc = 1;
        }
    }
    return rc;
}

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
    attr.max_rd_atomic = 16; //FIXME hardcoded!
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
    DEBUG_PRINT((stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid));

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
    DEBUG_PRINT((stdout, "Remote address = 0x%" PRIx64 "\n", remote_con_data.addr));
    DEBUG_PRINT((stdout, "Remote rkey = 0x%x\n", remote_con_data.rkey));
    DEBUG_PRINT((stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num));
    DEBUG_PRINT((stdout, "Remote LID = 0x%x\n", remote_con_data.lid));

    if (config.gid_idx >= 0){
        uint8_t *p = remote_con_data.gid;
        DEBUG_PRINT((stdout,
                "Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
                p[10], p[11], p[12], p[13], p[14], p[15]));
    }

    /* MODIFY QP STATE TO INIT */
    rc = modify_qp_to_init (res->qp);
    if (rc){
        fprintf (stderr, "change QP state to INIT failed\n");
        goto connect_qp_exit;
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

    DEBUG_PRINT((stdout, "Modified QP state to RTR\n"));

    rc = modify_qp_to_rts (res->qp);
    if (rc){
        fprintf (stderr, "failed to modify QP state to RTR\n");
        goto connect_qp_exit;
    }
    DEBUG_PRINT((stdout, "QP state was change to RTS\n"));


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
    sge.length = config.xfer_unit;
    sge.lkey = res->mr->lkey;
    /* prepare the receive work request */
    memset (&rr, 0, sizeof (rr));
    rr.next = NULL;
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;
    /* post the Receive Request to the RQ */
    rc = ibv_post_recv (res->qp, &rr, &bad_wr);
    if (rc){
        fprintf (stderr, "failed to post RR\n");
    } else {
        DEBUG_PRINT((stdout, "Receive Request was posted\n"));
    }
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
        if (res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection to server %s, port %d\n",
                    config.server_name, config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
    } else {
        DEBUG_PRINT((stdout, "waiting on port %d for TCP connection\n", config.tcp_port));
        res->sock = sock_connect (NULL, config.tcp_port);
        if (res->sock < 0){
            fprintf (stderr,
                    "failed to establish TCP connection with client on port %d\n",
                    config.tcp_port);
            rc = -1;
            goto resources_create_exit;
        }
    }
    DEBUG_PRINT((stdout, "TCP connection was established\n"));


    /* EXCHANGE CONFIG INFO */
    struct config_t *config_other = (struct config_t *) malloc( sizeof(struct config_t) );

    if( sock_sync_data( res->sock, sizeof(struct config_t), (char *) &config, (char *) config_other) < 0){
        fprintf(stderr, "failed to communicate demanded buffer size\n");
        rc = -1;
        goto resources_create_exit;
    }
    config.xfer_unit = MAX(config.xfer_unit, config_other->xfer_unit);
    config.iter = MAX(config.iter, config_other->iter);
    config.config_other = config_other;
    DEBUG_PRINT((stdout, "buffer %zd bytes, %d iterations\n", config.xfer_unit, config.iter));

    /* GET IB DEVICES AND SELECT ONE */
    DEBUG_PRINT((stdout, "searching for IB devices in host\n"));
    dev_list = ibv_get_device_list (&num_devices);
    if (!dev_list){
        fprintf (stderr, "failed to get IB devices list\n");
        rc = 1;
        goto resources_create_exit;
    }

    if (!num_devices){
        fprintf (stderr, "found %d device(s)\n", num_devices);
        rc = 1;
        goto resources_create_exit;
    }
    DEBUG_PRINT((stdout, "found %d device(s)\n", num_devices));

    for (i = 0; i < num_devices; i++)
    {
        if (!config.dev_name)
        {
            config.dev_name = strdup (ibv_get_device_name (dev_list[i]));
            DEBUG_PRINT((stdout,
                    "device not specified, using first one found: %s\n",
                    config.dev_name));
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
    size = config.xfer_unit;
    res->buf = (char *) malloc(size);
    if (!res->buf){
        fprintf (stderr, "failed to malloc %Zu bytes to memory buffer\n", size);
        rc = 1;
        goto resources_create_exit;
    }
    memset (res->buf, 0, size);

    /* REGISTER MEMORY BUFFER */
    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE;
    res->mr = ibv_reg_mr (res->pd, res->buf, size, mr_flags);
    if (!res->mr){
        fprintf (stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
        rc = 1;
        goto resources_create_exit;
    }
    DEBUG_PRINT((stdout,
            "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
            res->buf, res->mr->lkey, res->mr->rkey, mr_flags));


    /* CREATE QUEUE PAIR */
    memset (&qp_init_attr, 0, sizeof (qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    //qp_init_attr.sq_sig_all = 0; 
    qp_init_attr.send_cq = res->cq;
    qp_init_attr.recv_cq = res->cq;
    qp_init_attr.cap.max_send_wr = MAX_SEND_WR;
    qp_init_attr.cap.max_recv_wr = MAX_RECV_WR;
    qp_init_attr.cap.max_send_sge = MAX_SEND_SGE;
    qp_init_attr.cap.max_recv_sge = MAX_RECV_SGE;
    res->qp = ibv_create_qp (res->pd, &qp_init_attr);
    if (!res->qp)
    {
        fprintf (stderr, "failed to create QP\n");
        rc = 1;
        goto resources_create_exit;
    }
    DEBUG_PRINT((stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num));

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

        if (sockfd >= 0)
        {
            if (servername)
            {
                /* Client mode. Initiate connection to remote */
                if ((tmp =
                            connect (sockfd, iterator->ai_addr, iterator->ai_addrlen)))
                {
                    fprintf (stderr, "failed connect \n");
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

    while (!rc && total_read_bytes < xfer_size){
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
    char *op;
    char *crt;

    fprintf (stdout, YEL "CONFIG-------------------------------------------\n" );
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

static void crt_to_str(int code, char **str)
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

static void opcode_to_str(int opcode, char **str)
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
static void report_result(struct metrics_t met)
{
    float average, min, max;
    switch(config.crt){
        case CRT_BW:
            average = (float) config.xfer_unit / ((float) met.total / (float) config.iter);
            max = (float) config.xfer_unit / met.min;
            min = (float) config.xfer_unit / met.max;
            break;
        case CRT_LAT:
            average = (float) met.total / (float) config.iter;
            min = met.min;
            max = met.max;
            break;
/*         case CRT_CPU:
 *             break;
 *         case CRT_ALL:
 *             break;
 */
        default:
            fprintf(stdout, "NOT SUPPORTED YET\n");
            return;
    }
    fprintf(stdout, "%zd\t%d\t%f\t%f\t%f\n",config.xfer_unit, config.iter, min, max, average);
}

static void print_report(unsigned int iters, unsigned size, int duplex,
        int no_cpu_freq_fail)
{
    double cycles_to_units;
    unsigned long tsize;	/* Transferred size, in megabytes */
    int i, j;
    int opt_posted = 0, opt_completed = 0;
    cycles_t opt_delta;
    cycles_t t;

    opt_delta = tcompleted[opt_posted] - tposted[opt_completed];

    /* Find the peak bandwidth */
    for (i = 0; i < iters; ++i)
        for (j = i; j < iters; ++j) {
            t = (tcompleted[j] - tposted[i]) / (j - i + 1);
            if (t < opt_delta) {
                opt_delta  = t;
                opt_posted = i;
                opt_completed = j;
            }
        }
    cycles_to_units = get_cpu_mhz(no_cpu_freq_fail) * 1000000;
    tsize = duplex ? 2 : 1;
    tsize = tsize * size;
    printf(REPORT_FMT,size,iters,tsize * cycles_to_units / opt_delta / 0x100000,
            tsize * iters * cycles_to_units /(tcompleted[iters - 1] - tposted[0]) / 0x100000);
}

static void check_wc_status(enum ibv_wc_status status)
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

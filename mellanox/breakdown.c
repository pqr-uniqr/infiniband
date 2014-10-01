

"BREAKING DOWN RDMA_RC_EXAMPLE" -- hk110 


int main (int argc, char *argv[]) {
    struct resources res;
    int rc = 1;
    char temp_char;



    /* SET SHIT UP --------------------------------------------------------------------*/

//    according to the passed in arguments, we fill in the below structure
//
//    struct config_t
//    {
//        const char *dev_name;		/* IB device name */
//        char *server_name;		/* server host name */
//        u_int32_t tcp_port;		/* server TCP port */
//        int ib_port;			/* local IB port to work with */
//        int gid_idx;			/* gid index to use */
//    };


    /* REAL WORK STARTS --------------------------------------------------------------------*/



    /* init all of the resources, so cleanup will be easy */

    /* IN MAIN ------------------ */
    resources_init (&res);
    /* IN MAIN ------------------ */

        static void resources_init (struct resources *res){
            memset (res, 0, sizeof *res);
            res->sock = -1;
        }

    if (resources_create (&res)) {
        fprintf (stderr, "failed to create resources\n");
        goto main_exit;
    }

    /* resources_create */
        static int resources_create (struct resources *res) {
            // let's set up variables 
            struct ibv_device **dev_list = NULL;
            struct ibv_qp_init_attr qp_init_attr;
            struct ibv_device *ib_dev = NULL;
            size_t size;
            int i;
            int mr_flags = 0;
            int cq_size = 0;
            int num_devices;
            int rc = 0; // error flag

            // let's open a plain TCP connection--for IB connection establishment
            if (config.server_name) {
                //client?
                res->sock = sock_connect(config.server_name, config.tcp_port);
            } else {
                //server?
                res->sock = sock_connect (NULL, config.tcp_port);
            }

            // get local IB devices 
            dev_list = ibv_get_device_list (&num_devices);
            config.dev_name = strdup( ibv_get_device_name(dev_list[0]) );
            ib_dev = dev_list[0];

            /* get device handle */
            res->ib_ctx = ibv_open_device( ib_dev );

            /* We are now done with device list, free it */
            ibv_free_device_list (dev_list);
            dev_list = NULL;
            ib_dev = NULL;

            // query port on the other side
            ibv_query_port( res->ib_ctx, config.ib_port, &res->port_attr );

            // allocate protection domain 
            res->pd = ibv_alloc_pd( res->ib_ctx);

            // create completion queue (cq_size = 1)
            res->cq = ibv_create_cq( res->ib_ctx, cq_size, NULL, NULL, 0 );

            // allocate buffer for receiving data (size = MSG_SIZE)
            res->buf = (char *) malloc(size);
            memset(res->buf, 0, size);

            if (!config.server_name){
                strcpy( res->buf, "SEND operation" );
            }

            // register memory buffer
            mr_flags = IBV_ACCESS_LOCAL_WRITE | 
                IBV_ACCESS_REMOTE_READ | 
                IBV_ACCESS_REMOTE_WRITE;
            res->mr = ibv_reg_mr( res->pd, res->buf, size, mr_flags);

            fprintf (stdout,
                    "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
                    res->buf, res->mr->lkey, res->mr->rkey, mr_flags);

            /* create the Queue Pair */
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

            fprintf (stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num);
            return rc;
        }

    if (connect_qp (&res)) {
        fprintf (stderr, "failed to connect QPs\n");
        goto main_exit;
    }

    /* connect_qp */
    static int connect_qp (struct resources *res) {

        struct cm_con_data_t local_con_data;
        struct cm_con_data_t remote_con_data;
        struct cm_con_data_t tmp_con_data;
        int rc = 0;
        char temp_char;

        /* exchange using TCP sockets info required to connect QPs */
        local_con_data.addr = htonll ((uintptr_t) res->buf);
        local_con_data.rkey = htonl (res->mr->rkey);
        local_con_data.qp_num = htonl (res->qp->qp_num);
        local_con_data.lid = htons (res->port_attr.lid);
        memcpy (local_con_data.gid, &my_gid, 16);

        fprintf (stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid);

        sock_sync_data(res->sock, sizeof(struct cm_con_data_t), (char *) &local_con_data, 
                (char *) &tmp_con_data);

        remote_con_data.addr = ntohll (tmp_con_data.addr);
        remote_con_data.rkey = ntohl (tmp_con_data.rkey);
        remote_con_data.qp_num = ntohl (tmp_con_data.qp_num);
        remote_con_data.lid = ntohs (tmp_con_data.lid);
        memcpy (remote_con_data.gid, tmp_con_data.gid, 16);

        /* save the remote side attributes, we will need it for the post SR */
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
        /* modify the QP to init */
        rc = modify_qp_to_init (res->qp);
        /* let the client post RR to be prepared for incoming messages */
        if (config.server_name){
            rc = post_receive (res);
        }
        /* modify the QP to RTR */
        modify_qp_to_rtr (res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
        modify_qp_to_rts (res->qp);
        return rc;
    }



    /* after polling the completion we have the message in the client buffer too */
    if (config.server_name){
        fprintf (stdout, "Message is: '%s'\n", res.buf);
    } else {
        strcpy (res.buf, RDMAMSGR);
    }

    /* will block return until both client and server are here */
    sock_sync_data(res.sock, 1, "R", &temp_char);

    if(config.server_name){
        post_send( &res, IBV_WR_RDMA_READ );
        poll_completion( &res );
    }

    /* Now the client performs an RDMA read and then write on server.
       Note that the server has no idea these events have occured */
    if (config.server_name){
        /* First we read contens of server's buffer */


        if (poll_completion (&res)){
            fprintf (stderr, "poll completion failed 2\n");
            rc = 1;
            goto main_exit;
        }
        fprintf (stdout, "Contents of server's buffer: '%s'\n", res.buf);

        /* Now we replace what's in the server's buffer */
        strcpy (res.buf, RDMAMSGW);
        fprintf (stdout, "Now replacing it with: '%s'\n", res.buf);
        if (post_send (&res, IBV_WR_RDMA_WRITE))
        {
            fprintf (stderr, "failed to post SR 3\n");
            rc = 1;
            goto main_exit;
        }
        if (poll_completion (&res))
        {
            fprintf (stderr, "poll completion failed 3\n");
            rc = 1;
            goto main_exit;
        }
    }
    /* Sync so server will know that client is done mucking with its memory */
    if (sock_sync_data (res.sock, 1, "W", &temp_char))	/* just send a dummy char back and forth */
    {
        fprintf (stderr, "sync error after RDMA ops\n");
        rc = 1;
        goto main_exit;
    }
    if (!config.server_name)
        fprintf (stdout, "Contents of server buffer: '%s'\n", res.buf);
    rc = 0;
main_exit:
    if (resources_destroy (&res))
    {
        fprintf (stderr, "failed to destroy resources\n");
        rc = 1;
    }
    if (config.dev_name)
        free ((char *) config.dev_name);
    fprintf (stdout, "\ntest result is %d\n", rc);
    return rc;
}




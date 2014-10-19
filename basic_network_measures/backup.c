
    /* BELOW IS OLD CODE ------------------------------------------- */

    for( i=0; i < iter; i++){
        DEBUG_PRINT((stdout, YEL "trial no. %d ------------\n" RESET , i));

        /* GENERATE DATA */
        if( config.config_other->opcode == IBV_WR_RDMA_READ || 
                config.opcode == IBV_WR_RDMA_WRITE || config.opcode == IBV_WR_SEND ){
            FILE *random = fopen("/dev/urandom", "r");
            fread(res.buf, 1, config.xfer_unit, random);
            fclose(random);
#ifdef DEBUG
            csum = checksum(res.buf, config.xfer_unit);
            fprintf(stdout, WHT "checksum of data in my buffer: %0x\n" RESET, csum);
#endif
        }


        /* POST RECEIVE IF THE OTHER HAS PLANS TO DO SEND */
        if (config.config_other->opcode == IBV_WR_SEND ){
            if( (rc = post_receive(&res)) ){
                fprintf(stderr, "failed to post RR\n");
                goto main_exit;
            }
        }  

        /* WAIT TILL BOTH ARE ON THE SAME PAGE */
        if (sock_sync_data (res.sock, 1, "R", &temp_char)){
            fprintf (stderr, "sync error before RDMA ops\n");
            rc = 1;
            goto main_exit;
        }

        DEBUG_PRINT((stdout, GRN "sync finished--beginning operation\n" RESET ));

        /* DATA OPERATION */
        if( config.opcode == IBV_WR_RDMA_READ || 
                config.opcode == IBV_WR_RDMA_WRITE || config.opcode == IBV_WR_SEND ){

            gettimeofday(&cur_time, NULL);
            start_time_usec = (cur_time.tv_sec * 1000 * 1000) + cur_time.tv_usec;

            /* POST REQUEST */
            if ( post_send(&res, config.opcode) ){
                fprintf (stderr, "failed to post SR 2\n");
                rc = 1;
                goto main_exit;
            }

            /* POLL FOR COMPLETION */
            if ( poll_completion(&res) ){
                fprintf (stderr, "poll completion failed 2\n");
                rc = 1;
                goto main_exit;
            }

            gettimeofday(&cur_time, NULL);
            cur_time_usec = (cur_time.tv_sec * 1000 * 1000) + cur_time.tv_usec;

            d = cur_time_usec - start_time_usec;
            met.total += d;
            met.min = MIN(d, met.min);
            met.max = MAX(d, met.max);
        } 


        //TODO once this is both ways, this is not gonna work like this
        if( config.config_other->opcode == IBV_WR_SEND ){
            if (poll_completion (&res)){
                fprintf (stderr, "poll completion failed\n");
                goto main_exit;
            }
        } 


        if ( sock_sync_data (res.sock, 1, "D", &temp_char) ){
            fprintf (stderr, "sync error after RDMA ops\n");
            rc = 1;
            goto main_exit;
        }

#ifdef DEBUG
        csum = checksum( res.buf, config.xfer_unit );
        fprintf(stdout, WHT "final checksum inside my buffer: %0x\n" RESET, csum);
        fprintf(stdout, YEL "------------------------\n\n" RESET);
#endif

    }

    DEBUG_PRINT((stdout, GRN "data operation finished\n" RESET ));

    /* WAIT */

//main_exit: FIXME
    if (resources_destroy (&res)){
        fprintf (stderr, "failed to destroy resources\n");
        rc = 1;
    }
    if (config.dev_name) free ((char *) config.dev_name);
    if (config.config_other) free((char *)config.config_other);

    /* REPORT ON EXPERIMENT TO STDOUT */
    report_result( met );
    return rc;

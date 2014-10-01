/* 
 * author: hk110
 * rdma_mellanox_example.c modified for use in performance benchmark
 *
 * */


#include "rdma.h"

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  main
 *  Description:  
 * =====================================================================================
 */



    int
main ( int argc, char *argv[] )
{
    struct resource res;

    /* cl arguments: */

    while (1)
    {
        int c;
        static struct option long_options[] = {
            {.name = "port",.has_arg = 1,.val = 'p'},
            {.name = "ib-dev",.has_arg = 1,.val = 'd'},
            {.name = "ib-port",.has_arg = 1,.val = 'i'},
            {.name = "gid-idx",.has_arg = 1,.val = 'g'},
            {.name = NULL,.has_arg = 0,.val = '\0'}
        };
        c = getopt_long (argc, argv, "p:d:i:g:", long_options, NULL);
        if (c == -1)
            break;
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
            default:
                usage (argv[0]);
                return 1;
        }
    }
    /* parse the last parameter (if exists) as the server name */
    if (optind == argc - 1)
        config.server_name = argv[optind];
    else if (optind < argc)
    {
        usage (argv[0]);
        return 1;
    }
    /* print the used parameters for info*/
    print_config ();





    return EXIT_SUCCESS;
}				/* ----------  end of function main  ---------- */

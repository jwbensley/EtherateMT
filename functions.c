

uint8_t cli_args(int argc, char *argv[], struct etherate *etherate) {

    if (argc > 1) {

        for (uint16_t i = 1; i < argc; i += 1) {


            // Set frame size in ring buffer
            if (strncmp(argv[i], "-a", 2) == 0) {

                if (argc > (i+1)) {
                    etherate->frm_opt.block_frame_sz = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing frame allocation size.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set block size in ring buffer
            } else if (strncmp(argv[i], "-b", 2) == 0) {

                if (argc > (i+1)) {
                    etherate->frm_opt.block_sz = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing ring buffer block size.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set block number in ring buffer
            } else if (strncmp(argv[i], "-B", 2) == 0) {

                if (argc > (i+1)) {
                    etherate->frm_opt.block_nr = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing ring buffer block count.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }

            
            // Set number of worker threads
            } else if (strncmp(argv[i], "-c", 2) == 0) {

                if (argc > (i+1)) {
                    etherate->app_opt.num_threads = (uint32_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing number of threads.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Load a custom frame from file for Tx
            } else if (strncmp(argv[i], "-C", 2)==0) {
                if (argc > (i+1))
                {
                    FILE* frame_file = fopen(argv[i+1], "r");
                    if (frame_file == NULL){
                        perror("Opps! File loading error!");
                        return EXIT_FAILURE;
                    }

                    int16_t file_ret = 0;
                    etherate->frm_opt.frame_size = 0;
                    while (file_ret != EOF &&
                          (etherate->frm_opt.frame_size < frame_size_max)) {

                        file_ret = fscanf(frame_file, "%hhx", etherate->frm_opt.tx_buffer + etherate->frm_opt.frame_size);

                        if (file_ret == EOF) break;

                        etherate->frm_opt.frame_size += 1;
                    }

                    fclose(frame_file);

                    printf("Using custom frame (%d octets loaded)\n", etherate->frm_opt.frame_size);

                    etherate->frm_opt.custom_frame = 1;

                    if (etherate->frm_opt.frame_size > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required\n");
                    }
                    if (etherate->frm_opt.frame_size < 46) {
                        printf("WARNING: Minimum ethernet payload is 46 bytes, "
                               "Linux may pad the frame out to 46 bytes\n");
                    }

                    i += 1;

                } else {
                    printf("Oops! Missing filename\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Specifying frame payload size in bytes
            } else if (strncmp(argv[i], "-f", 2)==0) {

                if (argc > (i+1)) {

                    etherate->frm_opt.frame_size = (uint32_t)strtoul(argv[i+1], NULL, 0);

                    if (etherate->frm_opt.frame_size > frame_size_max) {
                        printf("The frame size is larger than the buffer max size"
                               " (%d bytes)!\n", frame_size_max);
                        return EX_SOFTWARE;
                    }

                    if (etherate->frm_opt.frame_size > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required\n");
                    }
                    if (etherate->frm_opt.frame_size < 46) {
                        printf("WARNING: Minimum ethernet payload is 46 bytes, "
                               "Linux may pad the frame out to 46 bytes\n");
                    }
                    
                    i += 1;

                } else {
                    printf("Oops! Missing frame size\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Display usage information
            } else if (strncmp(argv[i], "-h", 2)==0 ||
                       strncmp(argv[i], "--help", 6)==0) {

                print_usage();
                exit(EX_SOFTWARE);


            // Set interface by name
            } else if (strncmp(argv[i], "-i", 2)==0) {
                ///// If the interface name is wrong we quit with no error

                if (argc > (i+1)) {

                    strncpy((char*)etherate->sk_opt.if_name, argv[i+1], IFNAMSIZ);
                    etherate->sk_opt.if_index = get_if_index_by_name(etherate->sk_opt.if_name);
                    
                    if (etherate->sk_opt.if_index == -1) {
                        return EXIT_FAILURE;
                    }
                    
                    printf("Using inteface %s (%d)\n", etherate->sk_opt.if_name, etherate->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface name\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set interface by index number
            } else if (strncmp(argv[i], "-I", 2)==0) {

                if (argc > (i+1)) {

                    etherate->sk_opt.if_index = (uint32_t)strtoul(argv[i+1], NULL, 0);
                    get_if_name_by_index(etherate->sk_opt.if_index, etherate->sk_opt.if_name);

                    if (etherate->sk_opt.if_name[0]==0) {
                        return(EXIT_FAILURE);
                    }
                    
                    printf("Using inteface %s (%d)\n", etherate->sk_opt.if_name, etherate->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface index\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // List interfaces
            } else if (strncmp(argv[i], "-l", 2)==0) {

                get_if_list();
                return(EX_SOFTWARE);


            // Change to receive mode
            }else if (strncmp(argv[i], "-r" ,2)==0)  {
                etherate->app_opt.mode = 0; ///// Use global SK_TX?


            // Display version
            } else if (strncmp(argv[i], "-V", 2)==0 ||
                       strncmp(argv[i], "--version", 9)==0) {

                printf("Etherate version %s\n", app_version);
                exit(EX_SOFTWARE);


            // Unknown CLI arg
            } else {

                printf("Unknown CLI arg: %s\n", argv[i]);
                return EXIT_FAILURE;

            }

        }

    }

    return EXIT_SUCCESS;
}


void etherate_setup(struct etherate *etherate) {

    etherate->app_opt.mode            = 1;
    etherate->app_opt.num_threads     = 1;
    etherate->app_opt.thread_sk_affin = 0;
    etherate->app_opt.fanout_group_id = getpid() & 0xffff; // All fanout worker threads will 
                                                           // belong to the same fanout group
    
    etherate->frm_opt.block_frame_sz = block_frame_sz;
    etherate->frm_opt.block_nr       = block_nr;
    etherate->frm_opt.block_sz       = block_sz;
    etherate->frm_opt.custom_frame   = 0;
    etherate->frm_opt.frame_size     = frame_size;
    etherate->frm_opt.tx_buffer      = (uint8_t*)calloc(frame_size_max,1); // Used to load a custom frame payload
    
    etherate->sk_opt.if_index        = 0;
    memset(&etherate->sk_opt.if_name, 0, sizeof(IFNAMSIZ));


    return;
}


// Return interface index from name
int32_t get_if_index_by_name(uint8_t if_name[IFNAMSIZ]) {

    const int32_t retval = -1;

    int32_t sock_fd;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, (char*)if_name, IFNAMSIZ);
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr)==0)
    {
        close(sock_fd);
        return ifr.ifr_ifindex;
    }

    close(sock_fd);
    return retval;

}


// List available AF_PACKET interfaces and their index
void get_if_list() {

    struct ifreq ifreq;
    struct ifaddrs *ifaddr, *ifa;

    int sock_fd;
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (getifaddrs(&ifaddr)==-1) {
        perror("Couldn't getifaddrs() to list interfaces");
        exit(EX_PROTOCOL);
    }


    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {

        if (ifa->ifa_addr == NULL) {
            continue;
        }


        // Does this interface sub address family support AF_PACKET
        if (ifa->ifa_addr->sa_family==AF_PACKET) {

            // Set the ifreq by interface name
            strncpy(ifreq.ifr_name,ifa->ifa_name,sizeof(ifreq.ifr_name));

            // Does this device have a hardware address?
            if (ioctl (sock_fd, SIOCGIFHWADDR, &ifreq)==0) {

                uint8_t mac[6];
                memcpy(mac, ifreq.ifr_addr.sa_data, 6);

                // Get the interface index
                ioctl(sock_fd, SIOCGIFINDEX, &ifreq);

                // Print the current_cpu_setnt interface details
                printf("Device %s with address %02x:%02x:%02x:%02x:%02x:%02x, "
                       "has interface index %d\n", ifreq.ifr_name,
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                       ifreq.ifr_ifindex);

            }

        }

    }

    freeifaddrs(ifaddr);
    close(sock_fd);

    return;

}


// Return interface name from index
void get_if_name_by_index(int32_t if_index, uint8_t* if_name) {

    int32_t sock_fd;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_ifindex = if_index;
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock_fd, SIOCGIFNAME, &ifr)==0)
    {
        strncpy((char*)if_name, ifr.ifr_name, IFNAMSIZ);
        close(sock_fd);
        return;
    } else {
        memset(if_name, 0, IFNAMSIZ);
        close(sock_fd);
        return;
    }

}


// Accumulate and print traffic stats
void *print_pps(void *etherate_p) {

    struct etherate *etherate  = etherate_p;
    uint64_t duration          = 0;
    uint64_t rx_bytes_previous = 0;
    uint64_t rx_pkts_previous  = 0;
    uint64_t tx_bytes_previous = 0;
    uint64_t tx_pkts_previous  = 0;
    uint64_t rx_bytes_now      = 0;
    uint64_t rx_pkts_now       = 0;
    uint64_t tx_bytes_now      = 0;
    uint64_t tx_pkts_now       = 0;
    uint64_t rx_bytes          = 0;
    uint64_t rx_pps            = 0;
    uint64_t tx_bytes          = 0;
    uint64_t tx_pps            = 0;
    double tx_gbps             = 0;
    double rx_gbps             = 0;

    while(1) {

            rx_bytes_previous = 0;
            rx_pkts_previous  = 0;
            tx_bytes_previous = 0;
            tx_pkts_previous  = 0;
            rx_bytes_now      = 0;
            rx_pkts_now       = 0;
            tx_bytes_now      = 0;
            tx_pkts_now       = 0;            

        for(uint16_t thread = 0; thread < etherate->app_opt.num_threads; thread++) {

            rx_bytes_previous += etherate->thd_opt[thread].rx_bytes;
            rx_pkts_previous  += etherate->thd_opt[thread].rx_pkts;
            tx_bytes_previous += etherate->thd_opt[thread].tx_bytes;
            tx_pkts_previous  += etherate->thd_opt[thread].tx_pkts;
        }

        sleep(1);

        duration += 1;
        
        for(uint16_t thread = 0; thread < etherate->app_opt.num_threads; thread++) {

            rx_bytes_now += etherate->thd_opt[thread].rx_bytes;
            rx_pkts_now  += etherate->thd_opt[thread].rx_pkts;
            tx_bytes_now += etherate->thd_opt[thread].tx_bytes;
            tx_pkts_now  += etherate->thd_opt[thread].tx_pkts;

        }

        rx_bytes = rx_bytes_now - rx_bytes_previous;
        rx_pps   = rx_pkts_now  - rx_pkts_previous;
        tx_bytes = tx_bytes_now - tx_bytes_previous;
        tx_pps   = tx_pkts_now  - tx_pkts_previous;

        rx_gbps = ((double)(rx_bytes*8)/1000/1000/1000);
        tx_gbps = ((double)(tx_bytes*8)/1000/1000/1000);

        printf("%" PRIu64 ".\t%.2f Rx Gbps (%" PRIu64 " fps)\t%.2f Tx Gbps (%" PRIu64 " fps)\n", duration, rx_gbps, rx_pps, tx_gbps, tx_pps);

        ///// On thread quit print min/max/avg in Gbps and pps, also total GBs/TBs transfered?

    }
}


void print_usage () {

    printf ("Usage info;\n"
            "\t-a\tFrame allocation size in ring buffer.\n"
            "\t\tDefault is %" PRIu32 " bytes.\n"
            "\t-b\tBlock size in ring buffer. Default is %" PRIu32 " bytes\n"
            "\t-B\tBlock number in ring buffer. Default is %" PRIu32 ".\n"
            "\t-c\tNumber of worker threads to start. One more thread is\n"
            "\t\tstarted in addition to this value to print stats.\n"
            "\t\tDefault is 1.\n"
            "\t-C\tLoad a custom frame as hex from file.\n"
            "\t\tWhen not using -C the frame is random data.\n"
            "\t-f\tFrame size in bytes on the wire.\n"
            "\t\tThis has no effect when used with -C\n"
            "\t\tDefault is %" PRIu16 ", max %" PRIu16 ".\n"
            "\t-i\tSet interface by name.\n"
            "\t-I\tSet interface by index.\n"
            "\t-l\tList available interfaces.\n"
            "\t-r\tSwitch to Rx mode, the default is Tx.\n"
            "\t-V|--version Display version\n"
            "\t-h|--help Display this help text\n",
            block_frame_sz, block_sz, block_nr,
            frame_size, frame_size_max);

}


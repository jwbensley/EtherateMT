/*
 * License: MIT
 *
 * Copyright (c) 2017-2020 James Bensley.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */



#include "functions.h"



uint8_t cli_args(int argc, char *argv[], struct etherate *eth) {

    if (argc > 1) {

        for (uint16_t i = 1; i < argc; i += 1) {


            // Set frame size in ring buffer
            if (strncmp(argv[i], "-a", 2) == 0) {

                if (argc > (i+1)) {
                    eth->frm_opt.block_frm_sz = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing frame allocation size.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set block size in ring buffer
            } else if (strncmp(argv[i], "-b", 2) == 0) {

                if (argc > (i+1)) {
                    eth->frm_opt.block_sz = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing ring buffer block size.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set block number in ring buffer
            } else if (strncmp(argv[i], "-B", 2) == 0) {

                if (argc > (i+1)) {
                    eth->frm_opt.block_nr = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing ring buffer block count.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }

            
            // Set number of worker threads
            } else if (strncmp(argv[i], "-c", 2) == 0) {

                if (argc > (i+1)) {
                    eth->app_opt.thd_nr = (uint32_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing number of threads.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Load a custom frame from file for Tx
            } else if (strncmp(argv[i], "-C", 2) == 0) {
                if (argc > (i+1))
                {
                    FILE* frame_file = fopen(argv[i+1], "r");
                    if (frame_file == NULL){
                        perror("Opps! File opening error.\n");
                        return EXIT_FAILURE;
                    }

                    int16_t file_ret = 0;
                    eth->frm_opt.frame_sz = 0;
                    while (file_ret != EOF &&
                          (eth->frm_opt.frame_sz < DEF_FRM_SZ_MAX)) {

                        file_ret = fscanf(frame_file, "%" SCNx8, eth->frm_opt.tx_buffer + eth->frm_opt.frame_sz);

                        if (file_ret == EOF) break;

                        eth->frm_opt.frame_sz += 1;
                    }

                    if (fclose(frame_file) != 0) {
                        perror("Error closing file");
                        return EXIT_FAILURE;
                    }

                    printf("Using custom frame (%" PRIu16 " octets loaded):\n", eth->frm_opt.frame_sz);

                    for (uint16_t j = 0; j <= eth->frm_opt.frame_sz; j += 1) {
                        printf ("0x%" PRIx8 " ", eth->frm_opt.tx_buffer[j]);
                    }
                    printf("\n");

                    eth->frm_opt.custom_frame = 1;

                    if (eth->frm_opt.frame_sz > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required.\n");
                    } else if (eth->frm_opt.frame_sz < 46) {
                        printf("WARNING: Minimum ethernet payload is 46 bytes, "
                               "Linux may pad the frame out to 46 bytes.\n");
                    }

                    i += 1;

                } else {
                    printf("Oops! Missing filename.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Specifying frame payload size in bytes
            } else if (strncmp(argv[i], "-f", 2) == 0) {

                if (argc > (i+1)) {

                    eth->frm_opt.frame_sz = (uint32_t)strtoul(argv[i+1], NULL, 0);

                    if (eth->frm_opt.frame_sz > DEF_FRM_SZ_MAX) {
                        printf("Oops! The frame size is larger than the EtherateMT buffer size"
                               " (%" PRId32 " bytes).\n", DEF_FRM_SZ_MAX);
                        return EX_SOFTWARE;
                    }

                    if (eth->frm_opt.frame_sz > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required.\n");
                    } else if (eth->frm_opt.frame_sz < 46) {
                        printf("WARNING: Minimum ethernet payload is 46 bytes, "
                               "Linux may pad the frame out to 46 bytes.\n");
                    }
                    
                    i += 1;

                } else {
                    printf("Oops! Missing frame size.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Display usage information
            } else if (strncmp(argv[i], "-h", 2) == 0 ||
                       strncmp(argv[i], "--help", 6) == 0) {

                print_usage();
                exit(EX_SOFTWARE);


            // Set interface by name
            } else if (strncmp(argv[i], "-i", 2) == 0) {

                if (argc > (i+1)) {

                    strncpy((char*)eth->sk_opt.if_name, argv[i+1], IF_NAMESIZE);
                    eth->sk_opt.if_index = get_if_index_by_name(eth->sk_opt.if_name);
                    
                    if (eth->sk_opt.if_index == -1) {
                        printf("Opps! Can't find interface with name: %s.\n", argv[i+1]);
                        return EXIT_FAILURE;
                    }
                    
                    printf("Using inteface %s (%" PRId32 ").\n", eth->sk_opt.if_name, eth->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface name.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set interface by index number
            } else if (strncmp(argv[i], "-I", 2) == 0) {

                if (argc > (i+1)) {

                    eth->sk_opt.if_index = (uint32_t)strtoul(argv[i+1], NULL, 0);
                    get_if_name_by_index(eth->sk_opt.if_index, eth->sk_opt.if_name);

                    if (eth->sk_opt.if_name[0] == 0) {
                        printf("Opps! Can't find interface with index: %" PRIu32 ".\n", (uint32_t)strtoul(argv[i+1], NULL, 0));
                        return(EXIT_FAILURE);
                    }
                    
                    printf("Using inteface %s (%" PRId32 ").\n", eth->sk_opt.if_name, eth->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface index.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // List interfaces
            } else if (strncmp(argv[i], "-l", 2) == 0) {
                get_if_list();
                return(EX_SOFTWARE);


            // Set the number of packets to batch process with sendmmsg/recvmmsg
            } else if (strncmp(argv[i], "-m", 2) == 0) {

                if (argc > (i+1)) {
                    eth->sk_opt.msgvec_vlen = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing batch packet count.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Use send()/read() syscalls
            } else if (strncmp(argv[i], "-p0", 3) == 0) {

                eth->app_opt.sk_type = SKT_PACKET;


            // Use PACKET_MMAP with send()/poll() syscalls
            } else if (strncmp(argv[i], "-p1", 3) == 0) {

                eth->app_opt.sk_type = SKT_PACKET_MMAP2;


            // Use sendmsg()/recvmsg() syscalls
            } else if (strncmp(argv[i], "-p2", 3) == 0) {

                eth->app_opt.sk_type = SKT_SENDMSG;


            // Use sendmmsg()/recvmmsg() syscalls
            } else if (strncmp(argv[i], "-p3", 3) == 0) {

                eth->app_opt.sk_type = SKT_SENDMMSG;


            // Use PACKET_MMAP with send()/poll() syscalls
            } else if (strncmp(argv[i], "-p4", 3) == 0) {

                eth->app_opt.sk_type = SKT_PACKET_MMAP3;


            // Run in receive mode
            } else if (strncmp(argv[i], "-r" ,2) == 0)  {

                eth->app_opt.sk_mode = SKT_RX;


            // Run in bidirectional mode
            } else if (strncmp(argv[i], "-rt", 3) == 0) {

                eth->app_opt.sk_mode = SKT_BIDI;


            // Toggle strict thread/CPU affinity
            } else if (strncmp(argv[i], "-x", 2) == 0) {

                eth->app_opt.thd_affin = 1;


            // Enable verbose output
            } else if (strncmp(argv[i], "-v" ,2) == 0)  {

                eth->app_opt.verbose = 1;


            // Display version
            } else if (strncmp(argv[i], "-V", 2) == 0 ||
                       strncmp(argv[i], "--version", 9) == 0) {

                printf("Etherate version %s.\n", app_version);
                exit(EX_SOFTWARE);


            // Unknown CLI arg
            } else {

                printf("Oops! Unknown CLI arg: %s.\n", argv[i]);
                return EXIT_FAILURE;

            }

        }

    }

    return EXIT_SUCCESS;
}



void etherate_cleanup(struct etherate *eth) {

    if (eth->app_opt.thd != NULL)
        free(eth->app_opt.thd);
    
    if (eth->app_opt.thd_attr != NULL)
        free(eth->app_opt.thd_attr);

    if (eth->frm_opt.tx_buffer != NULL)
        free(eth->frm_opt.tx_buffer);

    if (eth->thd_opt != NULL)
        free(eth->thd_opt);

    rem_int_promisc(eth);

}



void etherate_setup(struct etherate *eth) {

    // All fanout worker threads will belong to the same fanout group
    eth->app_opt.err_len        = DEF_ERR_LEN;
    eth->app_opt.err_str        = NULL;
    eth->app_opt.fanout_grp     = getpid() & 0xffff;
    eth->app_opt.sk_mode        = SKT_TX;
    eth->app_opt.sk_type        = DEF_SKT_TYPE;
    eth->app_opt.thd            = NULL;
    eth->app_opt.thd_affin      = 0;
    eth->app_opt.thd_attr       = NULL;
    eth->app_opt.thd_nr         = DEF_THD_NR;
    eth->app_opt.verbose        = 0;
    
    eth->frm_opt.block_frm_sz   = DEF_BLK_FRM_SZ;
    eth->frm_opt.block_nr       = DEF_BLK_NR;
    eth->frm_opt.block_sz       = DEF_BLK_SZ;
    eth->frm_opt.custom_frame   = 0;
    eth->frm_opt.frame_nr       = 0;
    eth->frm_opt.frame_sz       = DEF_FRM_SZ;
    eth->frm_opt.tx_buffer      = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);

    if (eth->frm_opt.tx_buffer == NULL) {
        printf("Failed to calloc() per-thread buffers!\n");
        exit(EXIT_FAILURE);
    }
    
    eth->sk_opt.if_index        = -1;
    memset(&eth->sk_opt.if_name, 0, IF_NAMESIZE);
    eth->sk_opt.msgvec_vlen     = DEF_MSGVEC_LEN;

    eth->thd_opt                = NULL;

}



int32_t get_if_index_by_name(uint8_t if_name[IF_NAMESIZE]) {

    int32_t sock;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, (char*)if_name, IF_NAMESIZE);
    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock, SIOCGIFINDEX, &ifr)==0)
    {
        if (close(sock) == -1) {
            perror("Can't close socket");
            exit(EX_PROTOCOL);
        }
        return ifr.ifr_ifindex;
    }

    if (close(sock) == -1) {
        perror("Can't close socket");
        exit(EX_PROTOCOL);
    }

    return -1;

}



void get_if_list() {

    struct ifreq ifr;
    struct ifaddrs *ifaddr, *ifa;

    int sock;
    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (getifaddrs(&ifaddr) == -1) {
        perror("Can't get interface list");
        exit(EX_PROTOCOL);
    }


    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {

        if (ifa->ifa_addr == NULL) {
            continue;
        }


        // Does this interface sub address family support AF_PACKET
        if (ifa->ifa_addr->sa_family==AF_PACKET) {

            // Set the ifreq by interface name
            strncpy(ifr.ifr_name,ifa->ifa_name,sizeof(ifr.ifr_name));

            // Does this device have a hardware address?
            if (ioctl (sock, SIOCGIFHWADDR, &ifr) == 0) {

                uint8_t mac[6];
                memcpy(mac, ifr.ifr_addr.sa_data, 6);

                // Get the interface txqueuelen
                if (ioctl(sock, SIOCGIFTXQLEN, &ifr) == -1) {

                    perror("Can't get the interface txqueuelen");
                    if (close(sock) == -1) {
                        perror("Can't close socket");
                    }
                    exit(EX_PROTOCOL);

                }

                uint32_t txqueuelen = ifr.ifr_qlen;

                // Get the interface index
                if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {

                    perror("Can't get the interface index");
                    if (close(sock) == -1) {
                        perror("Can't close socket");
                    }
                    exit(EX_PROTOCOL);

                }

                // Print each interface's details
                printf("Device %s,"
                       " address %02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ","
                       " txqueuelen %" PRIu32 ","
                       " interface index %" PRId32 "\n",
                       ifr.ifr_name,
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                       txqueuelen,
                       ifr.ifr_ifindex);

            } 

        }

    }

    freeifaddrs(ifaddr);
    close(sock);

    return;

}



void get_if_name_by_index(int32_t if_index, uint8_t* if_name) { //// Why no return value on this function?

    int32_t sock;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_ifindex = if_index;
    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock, SIOCGIFNAME, &ifr)==0) {

        strncpy((char*)if_name, ifr.ifr_name, IF_NAMESIZE);
        if (close(sock) == -1) {
            perror("Can't close socket");
        }

        return;

    } else {
        
        memset(if_name, 0, IF_NAMESIZE);
        if (close(sock) == -1) {
            perror("Can't close socket");
        }
        
        return;
    }

}



void print_usage () {

    printf ("Usage info;\n"
            "\t-a\tAllocation size in bytes for each frame per block (for PACKET_MMAP).\n"
            "\t\tThis includes meta data. Default is %" PRId32 " bytes.\n"
            "\t-b\tBlock size in ring buffer (for PACKET_MMAP). Default is %" PRId32 " bytes.\n"
            "\t-B\tNumber of blocks in ring buffer (for PACKET_MMAP). Default is %" PRId32 ".\n"
            "\t-c\tNumber of worker threads to start. One more thread is started in addition\n"
            "\t\tto this value to print stats. Default is %" PRId32".\n"
            "\t-C\tLoad a custom frame from file formatted as hex bytes.\n"
            "\t\tDefault (when not using -C) the frame is random data.\n"
            "\t-f\tFrame size in bytes (excluding Preamble/SFD/CRC/IFG).\n"
            "\t\tThis has no effect when used with -C.\n"
            "\t\tDefault is %" PRId16 ", max %" PRId16 ".\n"
            "\t-i\tSet interface by name.\n"
            "\t-I\tSet interface by index.\n"
            "\t-l\tList available interfaces.\n"
            "\t-m\tSet the number of packets to batch process with sendmmsg()/recvmmsg().\n"
            "\t\tDefault is %" PRId16 ".\n"
            "\t-p[0-4]\tChose the Kernel send/receive method.\n"
            "\t-p0\tThis is the default send/receive mode, a single packet per send()/read() syscall.\n"
            "\t-p1\tSwith to PACKET_MMAP mode using PACKET_TX/RX_RING v2 to batch process a ring of packets.\n"
            "\t-p2\tSwitch to sendmsg()/recvmsg() syscalls per packet.\n"
            "\t-p3\tSwitch to sendmmsg()/recvmmsg() syscalls to batch process packets.\n"
            "\t-p4\tSwitch to PACKET_MMAP mode with PACKET_TX/RX_RING v3 to batch process a ring of packets.\n"
            "\t-[r|rt]\tThe default mode for a worker thread is transmit (Tx).\n"
            "\t-r\tRun the worker threads in receive (Rx) mode.\n"
            "\t-v\tEnable verbose output.\n"
            "\t-x\tLock worker threads to individual CPUs.\n"
            "\n"
            "\t-V|--version Display version\n"
            "\t-h|--help Display this help text\n",
            DEF_BLK_FRM_SZ, DEF_BLK_SZ, DEF_BLK_NR, DEF_THD_NR,
            DEF_FRM_SZ, DEF_FRM_SZ_MAX, DEF_MSGVEC_LEN);

}



int16_t rem_int_promisc(struct etherate *eth) {

    printf("Removing interface promiscuous mode\n");

    strncpy(eth->ifr.ifr_name, (char*)eth->sk_opt.if_name, IFNAMSIZ);


    int32_t sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1){
        perror("Can't open socket for promiscuous mode");
        return EX_SOFTWARE;
    }


    if (ioctl(sock, SIOCGIFFLAGS, &eth->ifr) == -1) {
        perror("Getting socket flags when removing promiscuous mode failed");
        if (close(sock) != 0)
            perror("Can't close socket for promiscuous mode");
        return EX_SOFTWARE;
    }

    eth->ifr.ifr_flags &= ~IFF_PROMISC;

    if (ioctl(sock, SIOCSIFFLAGS, &eth->ifr) == -1) {
        perror("Setting socket flags when removing promiscuous mode failed");
        if (close(sock) != 0)
            perror("Can't close socket for promiscuous mode");
        return EX_SOFTWARE;
    }


    if (close(sock) != 0)
        perror("Can't close socket for promiscuous mode");


    return EXIT_SUCCESS;

}



int16_t set_int_promisc(struct etherate *eth) {

    printf("Setting interface promiscuous mode\n");
    strncpy(eth->ifr.ifr_name, (char*)eth->sk_opt.if_name, IFNAMSIZ);

    int32_t sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1){
        perror("Can't open socket for promiscuous mode");
        return EX_SOFTWARE;
    }

    if (ioctl(sock, SIOCGIFFLAGS, &eth->ifr) == -1) {
        perror("Getting socket flags failed when setting promiscuous mode");
        if (close(sock) != 0)
            perror("Can't close socket for promiscuous mode");
        return EX_SOFTWARE;
    }

    eth->ifr.ifr_flags |= IFF_PROMISC;

    if (ioctl(sock, SIOCSIFFLAGS, &eth->ifr) == -1){
        perror("Setting socket flags failed when setting promiscuous mode");
        if (close(sock) != 0)
            perror("Can't close socket for promiscuous mode");
        return EX_SOFTWARE;
    }


    if (close(sock) != 0)
        perror("Can't close socket for promiscuous mode");


    return EXIT_SUCCESS;

}



void signal_handler(int signal) {

    struct etherate *eth = eth_p;

    printf("Quitting...\n");


    // Cancel worker threads and join them
    for(uint16_t thread = 0; thread < eth->app_opt.thd_nr; thread += 1) {

        int32_t pcancel = pthread_cancel(eth->app_opt.thd[thread]);
        if (pcancel != 0)
            printf(
                "Can't cancel worker thread %" PRIu32
                ", returned %" PRId32 "\n",
                eth->thd_opt[thread].thd_id, pcancel
            );

        void *thd_ret = NULL;
        pthread_join(eth->app_opt.thd[thread], &thd_ret);

    }

    // Cancel the stats thread and join it
    int32_t pcancel = pthread_cancel(eth->app_opt.thd[eth->app_opt.thd_nr]);
    if (pcancel != 0) {
        printf(
            "Can't cancel stats thread %" PRIu32 ", returned %" PRId32 "\n",
            eth->thd_opt[eth->app_opt.thd_nr].thd_id, pcancel
        );
    }

    void *thd_ret = NULL;
    pthread_join(eth->app_opt.thd[eth->app_opt.thd_nr], &thd_ret);

    etherate_cleanup(eth);
    exit(signal);

}
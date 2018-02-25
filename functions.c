/*
 * License: MIT
 *
 * Copyright (c) 2016-2018 James Bensley.
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



uint8_t cli_args(int argc, char *argv[], struct etherate *etherate) {

    if (argc > 1) {

        for (uint16_t i = 1; i < argc; i += 1) {


            // Set frame size in ring buffer
            if (strncmp(argv[i], "-a", 2) == 0) {

                if (argc > (i+1)) {
                    etherate->frm_opt.block_frm_sz = (uint16_t)strtoul(argv[i+1], NULL, 0);
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
                    etherate->app_opt.thd_cnt = (uint32_t)strtoul(argv[i+1], NULL, 0);
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
                        perror("Opps! File opening error!");
                        return EXIT_FAILURE;
                    }

                    int16_t file_ret = 0;
                    etherate->frm_opt.frame_sz = 0;
                    while (file_ret != EOF &&
                          (etherate->frm_opt.frame_sz < DEF_FRM_SZ_MAX)) {

                        file_ret = fscanf(frame_file, "%" SCNx8, etherate->frm_opt.tx_buffer + etherate->frm_opt.frame_sz); ///// %hhx

                        if (file_ret == EOF) break;

                        etherate->frm_opt.frame_sz += 1;
                    }

                    fclose(frame_file);
                    ///// Need to check fclose ret val?

                    printf("Using custom frame (%" PRIu16 " octets loaded):\n", etherate->frm_opt.frame_sz);

                    for (int i = 0; i <= etherate->frm_opt.frame_sz; i += 1) {
                        printf ("0x%" PRIx8 " ", etherate->frm_opt.tx_buffer[i]); ///// %hhx
                    }
                    printf("\n");

                    etherate->frm_opt.custom_frame = 1;

                    if (etherate->frm_opt.frame_sz > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required\n");
                    } else if (etherate->frm_opt.frame_sz < 46) {
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
            } else if (strncmp(argv[i], "-f", 2) == 0) {

                if (argc > (i+1)) {

                    etherate->frm_opt.frame_sz = (uint32_t)strtoul(argv[i+1], NULL, 0);

                    if (etherate->frm_opt.frame_sz > DEF_FRM_SZ_MAX) {
                        printf("The frame size is larger than the Etherate buffer size"
                               " (%" PRIi32 " bytes)!\n", DEF_FRM_SZ_MAX);
                        return EX_SOFTWARE;
                    }

                    if (etherate->frm_opt.frame_sz > 1514) {
                        printf("WARNING: Make sure your device supports baby "
                               "giants or jumbo frames as required\n");
                    } else if (etherate->frm_opt.frame_sz < 46) {
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
            } else if (strncmp(argv[i], "-h", 2) == 0 ||
                       strncmp(argv[i], "--help", 6) == 0) {

                print_usage();
                exit(EX_SOFTWARE);


            // Set interface by name
            } else if (strncmp(argv[i], "-i", 2) == 0) {
                ///// If the interface name is wrong we quit with no error!

                if (argc > (i+1)) {

                    strncpy((char*)etherate->sk_opt.if_name, argv[i+1], IF_NAMESIZE);
                    etherate->sk_opt.if_index = get_if_index_by_name(etherate->sk_opt.if_name);
                    
                    if (etherate->sk_opt.if_index == -1) {
                        return EXIT_FAILURE;
                    }
                    
                    printf("Using inteface %s (%" PRIi32 ")\n", etherate->sk_opt.if_name, etherate->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface name\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Set interface by index number
            } else if (strncmp(argv[i], "-I", 2) == 0) {

                if (argc > (i+1)) {

                    etherate->sk_opt.if_index = (uint32_t)strtoul(argv[i+1], NULL, 0);
                    get_if_name_by_index(etherate->sk_opt.if_index, etherate->sk_opt.if_name);

                    if (etherate->sk_opt.if_name[0] == 0) {
                        return(EXIT_FAILURE);
                    }
                    
                    printf("Using inteface %s (%" PRIi32 ")\n", etherate->sk_opt.if_name, etherate->sk_opt.if_index);
                    i += 1;

                } else {
                    printf("Oops! Missing interface index\n"
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
                    etherate->sk_opt.msgvec_vlen = (uint16_t)strtoul(argv[i+1], NULL, 0);
                    i += 1;
                } else {
                    printf("Oops! Missing back patch count.\n"
                           "Usage info: %s -h\n", argv[0]);
                    return EXIT_FAILURE;
                }


            // Use PACKET_MMAP with sendto()/poll() syscalls
            } else if (strncmp(argv[i], "-p1", 3) == 0) {

                etherate->app_opt.sk_type = SKT_PACKET_MMAP;


            // Use sendmsg()/recvmsg() syscalls
            } else if (strncmp(argv[i], "-p2", 3) == 0) {

                etherate->app_opt.sk_type = SKT_SENDMSG;


            // Use sendmmsg()/recvmmsg() syscalls
            } else if (strncmp(argv[i], "-p3", 3) == 0) {

                etherate->app_opt.sk_type = SKT_SENDMMSG;


            // Run in receive mode
            } else if (strncmp(argv[i], "-r" ,2) == 0)  {

                etherate->app_opt.sk_mode = SKT_RX;


            ///// Run in bidirectional mode
            } else if (strncmp(argv[i], "-rt", 3) == 0) {

                etherate->app_opt.sk_mode = SKT_BIDI;


            // Enable verbose output
            } else if (strncmp(argv[i], "-v" ,2) == 0)  {

                etherate->app_opt.verbose = 1;


            // Display version
            } else if (strncmp(argv[i], "-V", 2) == 0 ||
                       strncmp(argv[i], "--version", 9) == 0) {

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

    // All fanout worker threads will belong to the same fanout group
    etherate->app_opt.fanout_grp     = getpid() & 0xffff;
    etherate->app_opt.sk_mode        = SKT_TX;
    etherate->app_opt.sk_type        = DEF_SKT_TYPE;
    etherate->app_opt.thd_cnt        = 1;
    etherate->app_opt.thd_sk_affin   = 0;
    etherate->app_opt.verbose        = 0;
    
    etherate->frm_opt.block_frm_sz   = DEF_BLK_FRM_SZ;
    etherate->frm_opt.block_nr       = DEF_BLK_NR;
    etherate->frm_opt.block_sz       = DEF_BLK_SZ;
    etherate->frm_opt.custom_frame   = 0;
    etherate->frm_opt.frame_nr       = 0;
    etherate->frm_opt.frame_sz       = DEF_FRM_SZ;
    // Used to load a custom frame payload from file:
    etherate->frm_opt.tx_buffer      = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);
    
    etherate->sk_opt.if_index        = 0;
    memset(&etherate->sk_opt.if_name, 0, IF_NAMESIZE);
    etherate->sk_opt.msgvec_vlen      = DEF_MSGVEC_LEN;

}



int32_t get_if_index_by_name(uint8_t if_name[IF_NAMESIZE]) {

    #define ret -1;

    int32_t sock_fd;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, (char*)if_name, IF_NAMESIZE);
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr)==0)
    {
        if (close(sock_fd) == -1) {
            perror("Couldn't close socket");
            exit(EX_PROTOCOL);
        }
        return ifr.ifr_ifindex;
    }

    if (close(sock_fd) == -1) {
        perror("Couldn't close socket");
        exit(EX_PROTOCOL);
    }
    return ret;

}



void get_if_list() {

    struct ifreq ifreq;
    struct ifaddrs *ifaddr, *ifa;

    int sock_fd;
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (getifaddrs(&ifaddr) == -1) {
        perror("Couldn't get interface list");
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
            if (ioctl (sock_fd, SIOCGIFHWADDR, &ifreq) == 0) {

                uint8_t mac[6];
                memcpy(mac, ifreq.ifr_addr.sa_data, 6);

                // Get the interface index
                if (ioctl(sock_fd, SIOCGIFINDEX, &ifreq) == -1) {

                    perror("Couldn't get the interface index");
                    if (close(sock_fd) == -1) {
                        perror("Couldn't close socket");
                    }
                    exit(EX_PROTOCOL);

                }

                // Print the current_cpu_setnt interface details
                printf("Device %s with address "
                       "%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ", "
                       "has interface index %" PRIi32 "\n",
                       ifreq.ifr_name,
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                       ifreq.ifr_ifindex);

            } 

        }

    }

    freeifaddrs(ifaddr);
    close(sock_fd);

    return;

}



void get_if_name_by_index(int32_t if_index, uint8_t* if_name) {

    int32_t sock_fd;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_ifindex = if_index;
    sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (ioctl(sock_fd, SIOCGIFNAME, &ifr)==0)
    {
        strncpy((char*)if_name, ifr.ifr_name, IF_NAMESIZE);
        if (close(sock_fd) == -1) {
            perror("Couldn't close socket");
        }
        return;
    } else {
        memset(if_name, 0, IF_NAMESIZE);
        if (close(sock_fd) == -1) {
            perror("Couldn't close socket");
        }
        return;
    }

}



void *print_pps(void *etherate_p) {

    struct etherate *etherate  = etherate_p;
    /////struct tpacket_stats_v3      tpacket_stats;
    uint64_t duration          = 0;
    uint64_t rx_bytes_previous = 0;
    uint64_t rx_drops          = 0;
    uint64_t rx_pkts_previous  = 0;
    uint64_t rx_q_frz          = 0;
    uint64_t tx_bytes_previous = 0;
    uint64_t tx_drops          = 0;
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


    // Wait for one of the Tx/Rx threads to start
    /////
    /*
    while (1) {
        for(uint16_t thread = 0; thread < etherate->app_opt.thd_cnt; thread++) {
            if (etherate->thd_opt[thread].started == 1) break;
        }
    }
    */


    while(1) {

        rx_bytes_now      = 0;
        rx_drops          = 0;
        rx_pkts_now       = 0;
        rx_q_frz          = 0;
        tx_bytes_now      = 0;
        tx_drops          = 0;
        tx_pkts_now       = 0;

        for(uint16_t thread = 0; thread < etherate->app_opt.thd_cnt; thread++) {

            rx_bytes_now += etherate->thd_opt[thread].rx_bytes;
            rx_pkts_now  += etherate->thd_opt[thread].rx_pkts;
            tx_bytes_now += etherate->thd_opt[thread].tx_bytes;
            tx_pkts_now  += etherate->thd_opt[thread].tx_pkts;

            if(etherate->thd_opt[thread].sk_mode == SKT_TX) {
                
                // Not supported on TPACKET v3?
                /*
                socklen_t stats_len = sizeof(tpacket_stats);
                int32_t sock_stats = getsockopt(etherate->thd_opt[thread].sock_fd, SOL_PACKET, PACKET_STATISTICS, &sock_stats, &stats_len);
                if (sock_stats < 0) {
                    perror("Couldn't get Tx TPACKET socket stats");
                    exit(EXIT_FAILURE);
                }

                tx_drops += tpacket_stats.tp_drops;
                */

            } else if(etherate->thd_opt[thread].sk_mode == SKT_RX) {

                /*
                ///// Only if TPACKET V2 or V3 or V4 etc
                socklen_t stats_len = sizeof(tpacket_stats);
                int32_t sock_stats = getsockopt(etherate->thd_opt[thread].sock_fd, SOL_PACKET, PACKET_STATISTICS, &sock_stats, &stats_len);
                if (sock_stats < 0) {
                    perror("Couldn't get Rx TPACKET socket stats");
                    exit(EXIT_FAILURE);
                }

                rx_drops += tpacket_stats.tp_drops;
                rx_q_frz += tpacket_stats.tp_freeze_q_cnt;
                */

            }

        }


        rx_bytes = rx_bytes_now - rx_bytes_previous;
        rx_pps   = rx_pkts_now  - rx_pkts_previous;
        tx_bytes = tx_bytes_now - tx_bytes_previous;
        tx_pps   = tx_pkts_now  - tx_pkts_previous;

        rx_gbps = ((double)(rx_bytes*8)/1000/1000/1000);
        tx_gbps = ((double)(tx_bytes*8)/1000/1000/1000);

        if(etherate->app_opt.verbose) {
            printf("%" PRIu64 ".\t%.2f Rx Gbps (%" PRIu64 " fps) %lu Drops %lu Q-Freeze\t%.2f Tx Gbps (%" PRIu64 " fps) %" PRIu64 " Drops\n",
                   duration, rx_gbps, rx_pps, rx_drops, rx_q_frz, tx_gbps, tx_pps, tx_drops);
        } else {
            printf("%" PRIu64 ".\t%.2f Rx Gbps (%" PRIu64 " fps)\t%.2f Tx Gbps (%" PRIu64 " fps)\n", duration, rx_gbps, rx_pps, tx_gbps, tx_pps);
        }

        for(uint16_t thread = 0; thread < etherate->app_opt.thd_cnt; thread++) {

            rx_bytes_previous = rx_bytes_now;
            rx_pkts_previous  = rx_pkts_now;
            tx_bytes_previous = tx_bytes_now;
            tx_pkts_previous  = tx_pkts_now;
        }

        duration += 1;

        sleep(1);

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
            "\t-m\tSet the number of packets to batch process with sendmmsg()/recvmmsg().\n"
            "\t\tDefault is %" PRIu16 ".\n"
            "\t\tThe default send/receive mode processes a single packet per send()/read() syscall.\n" ///// Document this
            "\t-p1\tUse PACKET_MMAP with PACKET_TX/RX_RING to batch process a ring of packets.\n"
            "\t-p2\tUse sendmsg()/recvmsg() syscall per packet.\n"
            "\t-p3\tUse sendmmsg()/recvmmsg() to batch process packets.\n"
            "\t-r\tSwitch to Rx mode, the default is Tx.\n"
            "\t-rt\tSwith to bidirectional mode.\n"
            "\t-v\tEnable verbose output.\n"
            "\t-V|--version Display version\n"
            "\t-h|--help Display this help text\n",
            DEF_BLK_FRM_SZ, DEF_BLK_SZ, DEF_BLK_NR,
            DEF_FRM_SZ, DEF_FRM_SZ_MAX, DEF_MSGVEC_LEN); ///// Which PACKET_MMAP version is default? Add version to text.

}

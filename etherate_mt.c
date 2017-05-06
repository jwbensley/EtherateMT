#define _GNU_SOURCE          // Required for pthread_attr_setaffinity_np()
#include <errno.h>           // errno
#include <net/ethernet.h>    // ETH_P_ALL
#include <net/if.h>          // IF_NAMESIZE, struct ifreq
#include <linux/if_packet.h> // struct packet_mreq, tpacket_req, tpacket2_hdr, tpacket3_hdr, tpacket_req3
#include <ifaddrs.h>         // freeifaddrs(), getifaddrs()
#include <arpa/inet.h>       // htons()
#include <inttypes.h>        // PRIuN
#include <sys/ioctl.h>       // ioctl()
#include <math.h>            // floor()
#include <sys/mman.h>        // mmap()
#include <poll.h>            // poll()
#include <pthread.h>         // pthread_*()
#include <sys/socket.h>      // socket()
#include <stdlib.h>          // calloc(), exit(), EXIT_FAILURE, EXIT_SUCCESS, rand(), RAND_MAX, strtoul()
#include <stdio.h>           // FILE, fclose(), fopen(), fscanf(), perror(), printf()
#include <string.h>          // memcpy(), memset(), strncpy()
#include "sysexits.h"        // EX_NOPERM, EX_PROTOCOL, EX_SOFTWARE
#include <unistd.h>          // getpagesize(), getpid(), getuid(), read(), sleep()
#include <linux/version.h>   // KERNEL_VERSION(), LINUX_VERSION_CODE
#include "etherate_mt.h"
#include "functions.c"


int32_t setup_socket_mmap(struct thd_opt *thd_opt) {

    // Create a raw socket
    thd_opt->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock_fd == -1) {
        perror("Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Enable promiscuous mode
    struct packet_mreq packet_mreq;
    memset(&packet_mreq, 0, sizeof(packet_mreq));
    packet_mreq.mr_type    = PACKET_MR_PROMISC;
    packet_mreq.mr_ifindex = thd_opt->if_index;
    
    int32_t sock_promisc = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (void *)&packet_mreq, sizeof(packet_mreq));

    if (sock_promisc == -1) {
        perror("Can't enable promisc mode");
        return EXIT_FAILURE;
    }


    // Bind socket to interface.
    // This is mandatory (with zero copy) to know the header size of frames
    // used in the circular buffer:
    memset(&thd_opt->bind_addr, 0, sizeof(thd_opt->bind_addr));
    thd_opt->bind_addr.sll_family   = AF_PACKET;
    thd_opt->bind_addr.sll_protocol = htons(ETH_P_ALL);
    thd_opt->bind_addr.sll_ifindex  = thd_opt->if_index;

    int32_t sock_bind = bind(thd_opt->sock_fd, (struct sockaddr *)&thd_opt->bind_addr, sizeof(thd_opt->bind_addr));

    if (sock_bind == -1) {
        perror("Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Increase the socket Tx queue size so that the entire PACKET_MMAP Tx ring
    // can fit into the socket Tx queue. The Kerne will double the value provided
    // to allow for sk_buff overhead:
    if (thd_opt->sk_mode == SKT_TX) {

        int32_t sock_wmem_cur;
        socklen_t read_len = sizeof(sock_wmem_cur);
        if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur, &read_len) < 0) {
            perror("Can't get the socket write buffer size");
            return EXIT_FAILURE;
        }

        int32_t sock_wmem = (thd_opt->block_sz * thd_opt->block_nr);

        if (sock_wmem_cur < sock_wmem) {

            /////// Add verbose if?
            printf("Current socket write buffer size is %d bytes, desired write buffer size is %d bytes\n",
                    sock_wmem_cur, sock_wmem);

            printf("Trying to increase socket write buffer size to %d bytes...\n", sock_wmem);

            if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem, sizeof(sock_wmem)) < 0) {
                perror("Can't set the socket write buffer size");
                return EXIT_FAILURE;
            }
            
            if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur, &read_len) < 0) {
                perror("Can't get the socket write buffer size");
                return EXIT_FAILURE;
            }

            printf("Write buffer size set to %d bytes\n", sock_wmem_cur);


            if (sock_wmem_cur != sock_wmem) {

                printf("Trying to force the write buffer size to %d bytes...\n", sock_wmem);
                
                if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUFFORCE, &sock_wmem, sizeof(sock_wmem))<0) {
                    perror("Can't force the socket write buffer size");
                    return EXIT_FAILURE;
                }
                
                if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur, &read_len) < 0) {
                    perror("Can't get the socket write buffer size");
                    return EXIT_FAILURE;
                  }
                
                printf("Forced write buffer size is now %d bytes\n", sock_wmem_cur);
                if (sock_wmem_cur != sock_wmem) {
                    printf("Still smaller than desired!\n");
                }

            }

        }

    }


    // Bypass the kernel qdisc layer and push packets directly to the driver,
    // (packet are not buffered, tc disciplines are ignored, Tx support only).
    // This was added in Linux 3.14.
    if (thd_opt->sk_mode == SKT_TX) {

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
            
            static const int32_t sock_qdisc_bypass = 1;
            int32_t sock_qdisc_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));

            if (sock_qdisc_ret == -1) {
                perror("Can't enable QDISC bypass on socket");
                return EXIT_FAILURE;
            }

        #endif
    }


    // Enable packet loss, only supported on Tx
    ///// Add CLI arg to enabled and disable by default?
    if (thd_opt->sk_mode == SKT_TX) {
        /*
        static const int32_t sock_discard = 0;
        int32_t sock_loss_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_LOSS, (void *)&sock_discard, sizeof(sock_discard));

        if (sock_loss_ret == -1) {
            perror("Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
        }
        */
    }


    // Set the TPACKET version, v2 for Tx and v3 for Rx
    // (v2 supports packet level send(), v3 supports block level read())
    ///// Tx using TPACKET v3 is now in Kernel 4.11 !
    int32_t sock_pkt_ver = -1;

    if(thd_opt->sk_mode == SKT_TX) {
        static const int32_t sock_ver = TPACKET_V2;
        sock_pkt_ver = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_VERSION, &sock_ver, sizeof(sock_ver));
    } else {
        static const int32_t sock_ver = TPACKET_V3;
        sock_pkt_ver = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_VERSION, &sock_ver, sizeof(sock_ver));
    }

    if (sock_pkt_ver < 0) {
        perror("Can't set socket packet version");
        return EXIT_FAILURE;
    }
    

    // Ensure software timestamping is disabled
    static const int32_t sock_timestamp = 0;
    int32_t sock_ts_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TIMESTAMP, &sock_timestamp, sizeof(sock_timestamp));

    if (sock_ts_ret == -1) {
        perror("Can't disable timestamps on socket");
        return EXIT_FAILURE;
    }


    // Request HW timestamping 
    /*
      memset (&hwconfig, 0, sizeof (hwconfig));
      hwconfig.tx_type = HWTSTAMP_TX_OFF;
      hwconfig.rx_filter = HWTSTAMP_FILTER_ALL;

      timesource |= SOF_TIMESTAMPING_RAW_HARDWARE;
      timesource |= SOF_TIMESTAMPING_SYS_HARDWARE;

      memset (&ifr, 0, sizeof (ifr));
      strncpy (ifr.ifr_name, netdev, sizeof (ifr.ifr_name));
      ifr.ifr_data = (void *) &hwconfig;
      ret = ioctl (fd, SIOCSHWTSTAMP, &ifr);
      if (ret < 0)
        {
          perror ("ioctl SIOCSHWTSTAMP");
          // HW timestamps aren't support so will fall back to software
    }

      err =
        setsockopt (fd, SOL_PACKET, PACKET_TIMESTAMP, &timesource,
            sizeof (timesource));
      if (err < 0)
        {
          perror ("setsockopt PACKET_TIMESTAMP");
          exit (1);
    }
    */



    memset(&thd_opt->tpacket_req,  0, sizeof(struct tpacket_req));
    memset(&thd_opt->tpacket_req3, 0, sizeof(struct tpacket_req3));

    int32_t sock_mmap_ring = -1;
    if (thd_opt->sk_mode == SKT_TX) {

        thd_opt->tpacket_req.tp_block_size = thd_opt->block_sz;
        thd_opt->tpacket_req.tp_frame_size = thd_opt->block_frm_sz; // tp_frame_size = TPACKET2_HDRLEN + frame_sz
        thd_opt->tpacket_req.tp_block_nr   = thd_opt->block_nr;
        thd_opt->tpacket_req.tp_frame_nr   = thd_opt->frame_nr;

        sock_mmap_ring = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TX_RING, (void*)&thd_opt->tpacket_req, sizeof(struct tpacket_req));

    } else {

        thd_opt->tpacket_req3.tp_block_size = thd_opt->block_sz;
        thd_opt->tpacket_req3.tp_frame_size = thd_opt->block_frm_sz;
        thd_opt->tpacket_req3.tp_block_nr   = thd_opt->block_nr;
        thd_opt->tpacket_req3.tp_frame_nr   = thd_opt->frame_nr; ///// (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frm_sz;
        thd_opt->tpacket_req3.tp_retire_blk_tov   = 1; ////// Timeout in msec, what does this do?
        thd_opt->tpacket_req3.tp_feature_req_word = 0; //TP_FT_REQ_FILL_RXHASH;  ///// What does this do?

        sock_mmap_ring = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_RX_RING, (void*)&thd_opt->tpacket_req3, sizeof(thd_opt->tpacket_req3));
    }
    
    if (sock_mmap_ring == -1) {
        perror("Can't enable Tx/Rx ring for socket");
        return EXIT_FAILURE;
    }
    

    thd_opt->mmap_buf = NULL;
    thd_opt->rd       = NULL;

    if (thd_opt->sk_mode == SKT_TX) {

        thd_opt->mmap_buf = mmap(NULL, (thd_opt->block_sz * thd_opt->block_nr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED | MAP_POPULATE, thd_opt->sock_fd, 0);

        if (thd_opt->mmap_buf == MAP_FAILED) {
            perror("mmap failed");
            return EXIT_FAILURE;
        }


    } else {

        thd_opt->mmap_buf = mmap(NULL, (thd_opt->block_sz * thd_opt->block_nr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED | MAP_POPULATE, thd_opt->sock_fd, 0);

        if (thd_opt->mmap_buf == MAP_FAILED) {
            perror("mmap failed");
            return EXIT_FAILURE;
        }

        // Per bock rings in Rx mode (TPACKET_V3)
        thd_opt->rd = (struct iovec*)calloc(thd_opt->tpacket_req3.tp_block_nr * sizeof(struct iovec), 1);

        for (uint16_t i = 0; i < thd_opt->tpacket_req3.tp_block_nr; ++i) {
            thd_opt->rd[i].iov_base = thd_opt->mmap_buf + (i * thd_opt->tpacket_req3.tp_block_size);
            thd_opt->rd[i].iov_len  = thd_opt->tpacket_req3.tp_block_size;
        }
        

    }


    // Join this socket to the fanout group
    ///// Add if(){} for just one thread to not use fanout?
    
    uint16_t fanout_type = PACKET_FANOUT_CPU; 
    uint32_t fanout_arg = (thd_opt->fanout_group_id | (fanout_type << 16));
    int32_t  sock_fan_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg));

    if (sock_fan_ret < 0) {
        perror("Can't configure fanout");
        return EXIT_FAILURE;
    }


    return EXIT_SUCCESS;

}


void *packet_rx_mmap(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = setup_socket_mmap(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = thd_opt->sock_fd;
    pfd.events = POLLIN | POLLERR;
    pfd.revents = 0;


    uint32_t current_block_num = 0;
    struct block_desc *pbd = NULL;

    while (1) {
        pbd = (struct block_desc *) thd_opt->rd[current_block_num].iov_base;
 
        if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
            int32_t poll_ret = poll(&pfd, 1, -1);

            if (poll_ret == -1) {
                perror("Poll error");
                exit(EXIT_FAILURE);
            }

            continue;
        }


        uint32_t num_pkts = pbd->h1.num_pkts;
        uint32_t bytes = 0;
        struct tpacket3_hdr *ppd;

        ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd + pbd->h1.offset_to_first_pkt);

        for (uint32_t i = 0; i < num_pkts; ++i) {
            bytes += ppd->tp_snaplen;

            ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd + ppd->tp_next_offset);
        }


        thd_opt->rx_pkts += num_pkts;
        thd_opt->rx_bytes += bytes;

        pbd->h1.block_status = TP_STATUS_KERNEL;

        current_block_num = (current_block_num + 1) % thd_opt->block_nr;
    }   


}


void *packet_tx_mmap(void* thd_opt_p) {

    /*
    https://sites.google.com/site/packetmmap/
    How to adjust your performance

    Sending a jumbo frame of 1500 byte at 1Gbps last ~10µs.
    Sending a jumbo frame of 7500 byte at 1Gbps last ~50µs. 

    To insure the maximum bandwidth, the duration of the 
    sytem call / (number of ready packet * duration of packet transmission (ex : ~50µs or 10µs))
    must be inferior to 1.

    So you must play with the size of packets and the number of packets to send
    at each system call (send()). (This involves TX ring parameters and kernel latency limits). 
    */

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = setup_socket_mmap(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }
    
    /*
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = thd_opt->sock_fd;
    pfd.events = POLLOUT | POLLERR;
    pfd.revents = 0;
    */
    
    struct tpacket2_hdr *hdr;
    uint8_t *data;
    uint16_t i;
    int32_t tx_bytes = 0;

    while(1) {


        for (i = 0; i < thd_opt->frame_nr; i += 1) {
            hdr = (void*)(thd_opt->mmap_buf + (thd_opt->block_frm_sz * i));
            // TPACKET2_HDRLEN == (TPACKET_ALIGN(sizeof(struct tpacket2_hdr)) + sizeof(struct sockaddr_ll))
            // For raw Ethernet frames where the layer 2 headers are present
            // and the ring blocks are already aligned its fine to use:
            // sizeof(struct tpacket2_hdr)
            data = (uint8_t*)hdr + sizeof(struct tpacket2_hdr);
            memcpy(data, thd_opt->tx_buffer, thd_opt->frame_sz);
            hdr->tp_len = thd_opt->frame_sz;
            hdr->tp_status = TP_STATUS_SEND_REQUEST;
        }
        
        /*
        int32_t poll_ret = poll(&pfd, 1, -1);
        if (poll_ret == -1) {
            perror("Poll error");
            exit(EXIT_FAILURE);
        }
        //printf("Poll ret: %d\n", poll_ret);
        */

        ///// Any difference on > 4.1 kernel with real NIC?
        tx_bytes = sendto(thd_opt->sock_fd, NULL, 0, MSG_DONTWAIT, NULL, 0);

        if (tx_bytes == -1) {
            perror("sendto() error");
            exit(EXIT_FAILURE);
        }
        
        thd_opt->tx_pkts  += (tx_bytes / thd_opt->tpacket_req.tp_frame_nr);
        thd_opt->tx_bytes += tx_bytes;


    }

}



//void *print_pps_mmap(void * etherate_p) {
    //struct tpacket_stats_v3 stats; ///// Can we use this?
    // https://godoc.org/github.com/google/gopacket/afpacket#SocketStatsV3

//}



int32_t setup_socket(struct thd_opt *thd_opt) {

    // Create a raw socket
    thd_opt->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock_fd == -1) {
        perror("Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Enable promiscuous mode
    struct packet_mreq packet_mreq;
    memset(&packet_mreq, 0, sizeof(packet_mreq));
    packet_mreq.mr_type    = PACKET_MR_PROMISC;
    packet_mreq.mr_ifindex = thd_opt->if_index;
    
    int32_t sock_promisc = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (void *)&packet_mreq, sizeof(packet_mreq));

    if (sock_promisc == -1) {
        perror("Can't enable promisc mode");
        return EXIT_FAILURE;
    }


    // Bind socket to interface.
    // This is mandatory (with zero copy) to know the header size of frames
    // used in the circular buffer.
    memset(&thd_opt->bind_addr, 0, sizeof(thd_opt->bind_addr));
    thd_opt->bind_addr.sll_family   = AF_PACKET;
    thd_opt->bind_addr.sll_protocol = htons(ETH_P_ALL);
    thd_opt->bind_addr.sll_ifindex  = thd_opt->if_index;

    int32_t sock_bind = bind(thd_opt->sock_fd, (struct sockaddr *)&thd_opt->bind_addr, sizeof(thd_opt->bind_addr));

    if (sock_bind == -1) {
        perror("Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bypass the kernel qdisc layer and push packets directly to the driver,
    // (packet are not buffered, tc disciplines are ignored, Tx support only)
    if (thd_opt->sk_mode == SKT_TX) {

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
            
            static const int32_t sock_qdisc_bypass = 1;
            int32_t sock_qdisc_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));

            if (sock_qdisc_ret == -1) {
                perror("Can't enable QDISC bypass on socket");
                return EXIT_FAILURE;
            }

        #endif
        
    }


    // Ensure software timestamping is disabled
    static const int32_t sock_timestamp = 0;
    int32_t sock_ts_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TIMESTAMP, &sock_timestamp, sizeof(sock_timestamp));

    if (sock_ts_ret == -1) {
        perror("Can't enable QDISC bypass on socket");
        return EXIT_FAILURE;
    }


    // Enable packet loss, only supported on Tx
    if (thd_opt->sk_mode == SKT_TX) {
        
        static const int32_t sock_discard = 0;
        int32_t sock_loss_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_LOSS, (void *)&sock_discard, sizeof(sock_discard));

        if (sock_loss_ret == -1) {
            perror("Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
        }
    }


    // Join this socket to the fanout group
    int32_t fanout_type = PACKET_FANOUT_CPU; 
    int32_t fanout_arg = (thd_opt->fanout_group_id | (fanout_type << 16));
    int32_t setsockopt_fanout = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg));

    if (setsockopt_fanout < 0) {
        perror("Can't configure fanout!");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}



void *packet_rx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    int16_t rx_bytes;

    while(1) {

        rx_bytes = read(thd_opt->sock_fd, thd_opt->rx_buffer, frame_sz_max);
        
        if (rx_bytes == -1) {
            perror("Socket Rx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->rx_bytes += rx_bytes;
        thd_opt->rx_pkts += 1;

    }

}


void *packet_tx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    int16_t tx_bytes;

    while(1) {

        tx_bytes = sendto(thd_opt->sock_fd, thd_opt->tx_buffer,
                          thd_opt->frame_sz, 0,
                          (struct sockaddr*)&thd_opt->bind_addr,
                          sizeof(thd_opt->bind_addr));

        if (tx_bytes == -1) {
            perror("Socket Tx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->tx_bytes += tx_bytes;
        thd_opt->tx_pkts += 1;

    }

}


int main(int argc, char *argv[]) {

    // Global instance of all settings/values
    struct etherate etherate;

    // Create options and set application defaults    
    etherate_setup(&etherate);

    // Process CLI args
    uint16_t cli_args_ret = cli_args(argc, argv, &etherate);
    if (cli_args_ret == EXIT_FAILURE) {
        free(etherate.frm_opt.tx_buffer);
        return cli_args_ret;
    } else if (cli_args_ret == EX_SOFTWARE) {
        free(etherate.frm_opt.tx_buffer);
        return EXIT_SUCCESS;
    }

    // Check for root privileges
    if (getuid() != 0) {
        printf("Must be root to use this program!\n");
        return EX_NOPERM;
    }

    // Ensure an interface has been chosen
    if (etherate.sk_opt.if_index == 0) {
        printf("No interface chosen!\n");
        return EX_SOFTWARE;
    }

    ///// Add bidi mode?
    if (etherate.app_opt.mode == 0) {
        printf("Running in Rx mode\n");
    } else {
        printf("Running in Tx mode\n");
    }

    printf("Frame size set to %d bytes\n", etherate.frm_opt.frame_sz);

    ////// Move this to seperate function?
    if (etherate.app_opt.mode == SKT_TX) {

        // The frame allocation in the ring block holds the full layer 2 frame 
        // (headers + data) and some meta data, so it must hold TPACKET2_HDRLEN
        // (which is 52 bytes) aligned with TPACKET_ALIGN() (which increases it
        // from 52 to 64 bytes) + the minimum Ethernet layer 2 frame size (which
        // is 64 bytes):
        etherate.frm_opt.block_frm_sz = (etherate.frm_opt.frame_sz + TPACKET_ALIGN(TPACKET2_HDRLEN));

        // Blocks must contain at least 1 frame because frames can not be fragmented across blocks
        if (etherate.frm_opt.block_sz < etherate.frm_opt.block_frm_sz) {
            
            if (etherate.app_opt.verbose) {
                printf("Block size (%d) is less than block frame size (%d), padding to %d!\n.",
                       etherate.frm_opt.block_sz,
                       etherate.frm_opt.block_frm_sz,
                       etherate.frm_opt.block_frm_sz);
            }
            
            etherate.frm_opt.block_sz = etherate.frm_opt.block_frm_sz;
        }

        // Block size must be an integer multiple of pages
        if (
            (etherate.frm_opt.block_sz < getpagesize()) ||
            (etherate.frm_opt.block_sz % getpagesize() != 0)) {

            uint32_t base = (etherate.frm_opt.block_sz / getpagesize()) + 1;
            
            if (etherate.app_opt.verbose) {
                printf("Block size (%d) is not a multiple of the page size (%d), padding to %d!\n",
                        etherate.frm_opt.block_sz,
                        getpagesize(),
                        (base * getpagesize()));
            }
            
            etherate.frm_opt.block_sz = (base * getpagesize());
        }


        // The block frame size must be a multiple of TPACKET_ALIGNMENT (16) also,
        // the following integer math occurs in af_packet.c, the remainder is lost so the number
        // of frames per block MUST be either 1 or a power of 2:
        // packet_set_ring(): rb->block_frm_nr = req->tp_block_size / req->tp_frame_size;
        // packet_set_ring(): Checks if (block_frm_nr == 0) return EINVAL;
        // packet_set_ring(): checks if (block_frm_nr * tp_block_nr != tp_frame_nr) return EINVAL;
        uint32_t block_frm_nr = etherate.frm_opt.block_sz / etherate.frm_opt.block_frm_sz;
        uint32_t next_power = 0;
        uint32_t is_power_of_two = (block_frm_nr != 0) && ((block_frm_nr & (block_frm_nr - 1)) == 0 );

        if ((block_frm_nr != 1) && (is_power_of_two != 1)) {

            if (etherate.app_opt.verbose) {
                printf("Frames per block (%u) must be 1 or a power of 2!\n", block_frm_nr);
            }

            next_power = block_frm_nr;
            next_power--;
            next_power |= next_power >> 1;
            next_power |= next_power >> 2;
            next_power |= next_power >> 4;
            next_power |= next_power >> 8;
            next_power |= next_power >> 16;
            next_power++;

            etherate.frm_opt.block_sz = next_power * etherate.frm_opt.block_frm_sz;            

            if (etherate.frm_opt.block_sz % getpagesize() != 0) {
                uint32_t base = (etherate.frm_opt.block_sz / getpagesize()) + 1;
                etherate.frm_opt.block_sz = (base * getpagesize());
            }

            etherate.frm_opt.block_frm_sz = etherate.frm_opt.block_sz / next_power;
            block_frm_nr = etherate.frm_opt.block_sz / etherate.frm_opt.block_frm_sz;

            if (etherate.app_opt.verbose) {
                printf("Frames per block increased to %d and block size increased to %u.\n",
                        block_frm_nr, etherate.frm_opt.block_sz);

                printf("Block frame size increased to %u to evenly fill block.\n",
                        etherate.frm_opt.block_frm_sz);
            }

        // If the number of frames per block is already 1 or a power of 2, the frame block size must fill the block exactly,
        // use integer math to round off the frame block size to an exact multiple of the block size:
        } else if (
            (etherate.frm_opt.block_sz / etherate.frm_opt.block_frm_sz != 1) ||
            (etherate.frm_opt.block_sz % etherate.frm_opt.block_frm_sz != 0)) {

             uint32_t base = etherate.frm_opt.block_sz / etherate.frm_opt.block_frm_sz;
             etherate.frm_opt.block_frm_sz = etherate.frm_opt.block_sz / base;

             if (etherate.app_opt.verbose) {
                 printf("Block frame size increased to %u to evenly fill block.\n",
                        etherate.frm_opt.block_frm_sz);
             }
        }


        block_frm_nr = etherate.frm_opt.block_sz / etherate.frm_opt.block_frm_sz;
        etherate.frm_opt.frame_nr = (etherate.frm_opt.block_sz * etherate.frm_opt.block_nr) / etherate.frm_opt.block_frm_sz;
        if (etherate.app_opt.verbose) {
            printf("Frame block size %d, frames per block %d, block number %d, block size %d, frames in ring %d\n",
                   etherate.frm_opt.block_frm_sz,
                   block_frm_nr,
                   etherate.frm_opt.block_nr,
                   etherate.frm_opt.block_sz,
                   etherate.frm_opt.frame_nr);
        }

        if ((block_frm_nr * etherate.frm_opt.block_nr) != etherate.frm_opt.frame_nr) {
            printf("Frames per block (%d) * block number (%d) != frame number in ring (%d)!\n", block_frm_nr, etherate.frm_opt.block_nr, etherate.frm_opt.frame_nr);
        }

    }


    // Fill the test frame buffer with random data
    if (etherate.frm_opt.custom_frame == 0) {
        for (uint16_t i = 0; i < etherate.frm_opt.frame_sz; i += 1)
        {
            etherate.frm_opt.tx_buffer[i] = (uint8_t)((255.0*rand()/(RAND_MAX+1.0)));
        }
    }


    pthread_t worker_thread[etherate.app_opt.num_threads];
    pthread_attr_t attr[etherate.app_opt.num_threads];

    // Create a set of arguments for each worker thread
    etherate.thd_opt = calloc(sizeof(struct thd_opt), etherate.app_opt.num_threads);

    for (uint16_t thread = 0; thread < etherate.app_opt.num_threads; thread += 1) {

        pthread_attr_init(&attr[thread]);
        pthread_attr_setdetachstate(&attr[thread], PTHREAD_CREATE_JOINABLE);

        ///// Set thread priorities?
        /*
        pthread_attr_init(&t_attr_send);
        pthread_attr_init(&t_attr_fill);
     
        pthread_attr_setschedpolicy(&t_attr_send,SCHED_RR);
        pthread_attr_setschedpolicy(&t_attr_fill,SCHED_RR);
     
        para_send.sched_priority=20;
        pthread_attr_setschedparam(&t_attr_send,&para_send);
        para_fill.sched_priority=20;
        pthread_attr_setschedparam(&t_attr_fill,&para_fill);
        */

        // Set up and copy per-thread settings
        etherate.thd_opt[thread].block_nr        = etherate.frm_opt.block_nr;
        etherate.thd_opt[thread].block_sz        = etherate.frm_opt.block_sz;
        etherate.thd_opt[thread].block_frm_sz    = etherate.frm_opt.block_frm_sz;
        etherate.thd_opt[thread].frame_nr        = etherate.frm_opt.frame_nr;
        etherate.thd_opt[thread].fanout_group_id = etherate.app_opt.fanout_group_id;
        etherate.thd_opt[thread].if_index        = etherate.sk_opt.if_index;
        etherate.thd_opt[thread].tx_buffer       = etherate.frm_opt.tx_buffer;
        etherate.thd_opt[thread].frame_sz        = etherate.frm_opt.frame_sz;
        etherate.thd_opt[thread].frame_sz_max    = frame_sz_max;
        etherate.thd_opt[thread].rx_buffer       = (uint8_t*)calloc(frame_sz_max,1);
        etherate.thd_opt[thread].tx_buffer       = (uint8_t*)calloc(frame_sz_max,1);
        memcpy(etherate.thd_opt[thread].tx_buffer, etherate.frm_opt.tx_buffer, frame_sz_max);

        ///// Perhaps get the thread to set its own affinity first before it does anything else?
        if (etherate.app_opt.thread_sk_affin) {
            cpu_set_t current_cpu_set;

            int cpu_to_bind = thread % etherate.app_opt.num_threads;
            CPU_ZERO(&current_cpu_set);
            // We count cpus from zero
            CPU_SET(cpu_to_bind, &current_cpu_set);

            /////int set_affinity_result = pthread_attr_setaffinity_np(thread_attrs.native_handle(), sizeof(cpu_set_t), &current_cpu_set);
            int set_affinity_result = pthread_attr_setaffinity_np(&attr[thread], sizeof(cpu_set_t), &current_cpu_set);

            if (set_affinity_result != 0) {
                printf("Can't set CPU affinity for thread\n");
            } 
        }


        uint32_t worker_thread_ret = 0;
        if (etherate.app_opt.mode) {
            etherate.thd_opt[thread].sk_mode = SKT_TX;
            //worker_thread_ret = pthread_create(&worker_thread[thread], &attr[thread], packet_tx, (void*)&etherate.thd_opt[thread]);
            worker_thread_ret = pthread_create(&worker_thread[thread], &attr[thread], packet_tx_mmap, (void*)&etherate.thd_opt[thread]);
        } else {
            etherate.thd_opt[thread].sk_mode = SKT_RX;
            //worker_thread_ret = pthread_create(&worker_thread[thread], &attr[thread], packet_rx, (void*)&etherate.thd_opt[thread]);
            worker_thread_ret = pthread_create(&worker_thread[thread], &attr[thread], packet_rx_mmap, (void*)&etherate.thd_opt[thread]);
        }

        if (worker_thread_ret) {
          printf("Return code from worker pthread_create() is %d\n", worker_thread_ret);
          exit(EXIT_FAILURE);
        }

    }


    // Spawn a stats printing thread
    pthread_t stats_thread;
    int32_t stats_thread_ret = pthread_create(&stats_thread, NULL, print_pps, (void*)&etherate);


    // Free attribute and wait for the worker threads to finish
    for(uint16_t thread = 0; thread < etherate.app_opt.num_threads; thread += 1) {
        
        pthread_attr_destroy(&attr[thread]);
        
        void *thread_status;

        int32_t worker_join_ret = pthread_join(worker_thread[thread], &thread_status);
        
        munmap(etherate.thd_opt[thread].mmap_buf, (etherate.thd_opt[thread].block_sz * etherate.thd_opt[thread].block_nr));
        close(etherate.thd_opt[thread].sock_fd);
        free(etherate.thd_opt[thread].rx_buffer);
        free(etherate.thd_opt[thread].tx_buffer);

        if (worker_join_ret) {
            printf("Return code from pthread_join() is %d\n", worker_join_ret);
            exit(EXIT_FAILURE);
        }
        printf("Main: completed join with thread %d having a status of %ld\n", thread, (long)thread_status);
    }

    free(etherate.thd_opt);
    free(etherate.frm_opt.tx_buffer);


    void *thread_status;
    stats_thread_ret = pthread_join(stats_thread, &thread_status);
    if (stats_thread_ret) {
        printf("Return code from pthread_join() is %d\n", stats_thread_ret);
        exit(EXIT_FAILURE);
    }


}
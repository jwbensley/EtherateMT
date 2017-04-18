#define _GNU_SOURCE          // Required for pthread_attr_setaffinity_np()
#include <errno.h>           // errno
#include <net/ethernet.h>    // ETH_P_ALL
#include <net/if.h>          // IF_NAMESIZE, struct ifreq
#include <linux/if_packet.h> // struct packet_mreq, tpacket_req, tpacket2_hdr, tpacket3_hdr, tpacket_req3
#include <ifaddrs.h>         // freeifaddrs(), getifaddrs()
#include <arpa/inet.h>       // htons()
#include <inttypes.h>        // PRIuN
#include <sys/ioctl.h>       // ioctl()
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


    // Set the TPACKET version, v2 for Tx and v3 for Rx
    // (v2 supports packet level send(), v3 supports block level read())
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


    memset(&thd_opt->tpacket_req,  0, sizeof(struct tpacket_req));
    memset(&thd_opt->tpacket_req3, 0, sizeof(struct tpacket_req3));

    //thd_opt->block_sz = 4096; // (8388608 / 2048 = 4096 frames packets per block)
    // Could equal frame_size+TPACKET2_HDRLEN ?
    //thd_opt->block_nr = 256;
    //thd_opt->block_frame_sz = 4096;
    // frame_nr == (blocksiz * blocknum) / framesiz == 524288 frames in all bocks

    int32_t page_size = getpagesize();
    if (thd_opt->block_sz % page_size != 0) printf("Block size is not a multiple of getpagesize() (%d)!\n", getpagesize());
    if (thd_opt->block_frame_sz  < TPACKET2_HDRLEN) printf("Frame size is less than TPACKET2_HDRLEN (%lu)!\n", TPACKET2_HDRLEN);
    if (thd_opt->block_frame_sz % TPACKET_ALIGNMENT != 0) printf("Frame size (%d) is not a multiple of TPACKET_ALIGNMENT (%d)!\n", thd_opt->block_frame_sz, TPACKET_ALIGNMENT);

    int32_t sock_mmap_ring = -1;
    if (thd_opt->sk_mode == SKT_TX) {

        thd_opt->tpacket_req.tp_block_size = thd_opt->block_sz;
        thd_opt->tpacket_req.tp_frame_size = thd_opt->block_sz;
        thd_opt->tpacket_req.tp_block_nr   = thd_opt->block_nr;
        // Allocate per-frame blocks in Tx mode (TPACKET_V2)
        thd_opt->tpacket_req.tp_frame_nr   = thd_opt->block_nr;

        sock_mmap_ring = setsockopt(thd_opt->sock_fd, SOL_PACKET , PACKET_TX_RING , (void*)&thd_opt->tpacket_req , sizeof(struct tpacket_req));

    } else {

        thd_opt->tpacket_req3.tp_block_size = thd_opt->block_sz;
        thd_opt->tpacket_req3.tp_frame_size = thd_opt->block_frame_sz;
        thd_opt->tpacket_req3.tp_block_nr   = thd_opt->block_nr;
        thd_opt->tpacket_req3.tp_frame_nr   = (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frame_sz;
        thd_opt->tpacket_req3.tp_retire_blk_tov   = 1; ////// Timeout in msec, what does this do?
        thd_opt->tpacket_req3.tp_feature_req_word = 0; //TP_FT_REQ_FILL_RXHASH;  ///// What does this do?

        sock_mmap_ring = setsockopt(thd_opt->sock_fd, SOL_PACKET , PACKET_RX_RING , (void*)&thd_opt->tpacket_req3 , sizeof(thd_opt->tpacket_req3));
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
    ///// Add if for just one thread to not use fanout
    
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


        for (i = 0; i < thd_opt->tpacket_req.tp_frame_nr; i += 1) {

            hdr = (void*)(thd_opt->mmap_buf + (thd_opt->tpacket_req.tp_frame_size * i));
            data = (uint8_t*)(hdr + TPACKET_ALIGN(TPACKET2_HDRLEN));
            memcpy(data, thd_opt->tx_buffer, thd_opt->frame_size);
            hdr->tp_len = thd_opt->frame_size;
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

        tx_bytes = sendto(thd_opt->sock_fd, NULL, 0, 0, NULL, 0);

        if (tx_bytes == -1) {
            perror("sendto error");
            exit(EXIT_FAILURE);
        }
        
        thd_opt->tx_pkts  += thd_opt->tpacket_req.tp_frame_nr;
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
        
        static const int32_t sock_qdisc_bypass = 1;
        int32_t sock_qdisc_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));

        if (sock_qdisc_ret == -1) {
            perror("Can't enable QDISC bypass on socket");
            return EXIT_FAILURE;
        }
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

        rx_bytes = read(thd_opt->sock_fd, thd_opt->rx_buffer, frame_size_max);
        
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
                          thd_opt->frame_size, 0,
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

    // Fill the test frame buffer with random data
    if (etherate.frm_opt.custom_frame == 0) {
        for (uint16_t i = 0; i < etherate.frm_opt.frame_size; i += 1)
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

        ///// "set up thread options"
        etherate.thd_opt[thread].block_nr = etherate.frm_opt.block_nr;
        etherate.thd_opt[thread].block_sz = etherate.frm_opt.block_sz;
        etherate.thd_opt[thread].block_frame_sz = etherate.frm_opt.block_frame_sz;
        etherate.thd_opt[thread].fanout_group_id = etherate.app_opt.fanout_group_id;
        etherate.thd_opt[thread].if_index = etherate.sk_opt.if_index;
        etherate.thd_opt[thread].tx_buffer = etherate.frm_opt.tx_buffer;
        etherate.thd_opt[thread].frame_size = etherate.frm_opt.frame_size;
        etherate.thd_opt[thread].frame_size_max = frame_size_max;
        etherate.thd_opt[thread].rx_buffer = (uint8_t*)calloc(frame_size_max,1);
        etherate.thd_opt[thread].tx_buffer = (uint8_t*)calloc(frame_size_max,1);
        memcpy(etherate.thd_opt[thread].tx_buffer, etherate.frm_opt.tx_buffer, frame_size_max);

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
          exit(-1);
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
            exit(-1);
        }
        printf("Main: completed join with thread %d having a status of %ld\n", thread, (long)thread_status);
    }

    free(etherate.thd_opt);
    free(etherate.frm_opt.tx_buffer);


    void *thread_status;
    stats_thread_ret = pthread_join(stats_thread, &thread_status);
    if (stats_thread_ret) {
        printf("Return code from pthread_join() is %d\n", stats_thread_ret);
        exit(-1);
    }


}
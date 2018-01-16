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



#include "tpacket_v2.h"



void *rx_tpacket_v2(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    /////printf("This is thread %d\n", gettid());

    int32_t sk_setup_ret = tpacket_v2_sock(thd_opt_p);
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
    /////thd_opt->started = 1;

    while (1) {
        pbd = (struct block_desc *) thd_opt->rd[current_block_num].iov_base;
 
        if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
            int32_t poll_ret = poll(&pfd, 1, -1);

            if (poll_ret == -1) {
                perror("Rx poll error");
                exit(EXIT_FAILURE);
            }

            continue;
        }


        uint32_t num_pkts = pbd->h1.num_pkts;
        uint32_t bytes = 0;
        struct tpacket3_hdr *ppd; ////// change to version 5

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



void tpacket_v2_ring(struct etherate *etherate) {

    /*
     * The frame allocation in the ring block holds the full layer 2 frame 
     * (headers + data) and some meta data, so it must hold TPACKET2_HDRLEN
     * (which is 52 bytes) aligned with TPACKET_ALIGN() (which increases it
     * from 52 to 64 bytes) + the minimum Ethernet layer 2 frame size (which
     * is 64 bytes):
     */

    etherate->frm_opt.block_frm_sz = (etherate->frm_opt.frame_sz + TPACKET_ALIGN(TPACKET2_HDRLEN));


    // Blocks must contain at least 1 frame because frames can not be fragmented across blocks
    if (etherate->frm_opt.block_sz < etherate->frm_opt.block_frm_sz) {
        
        if (etherate->app_opt.verbose)
            printf("Block size (%d) is less than block frame size (%d), padding to %d!\n.",
                   etherate->frm_opt.block_sz,
                   etherate->frm_opt.block_frm_sz,
                   etherate->frm_opt.block_frm_sz);
        
        etherate->frm_opt.block_sz = etherate->frm_opt.block_frm_sz;
    }


    // Block size must be an integer multiple of pages
    if ((etherate->frm_opt.block_sz < getpagesize()) ||
        (etherate->frm_opt.block_sz % getpagesize() != 0)) {

        uint32_t base = (etherate->frm_opt.block_sz / getpagesize()) + 1;
        
        if (etherate->app_opt.verbose)
            printf("Block size (%d) is not a multiple of the page size (%d), padding to %d!\n",
                    etherate->frm_opt.block_sz,
                    getpagesize(),
                    (base * getpagesize()));
        
        etherate->frm_opt.block_sz = (base * getpagesize());
    }


    /*
     * The block frame size must be a multiple of TPACKET_ALIGNMENT (16) also,
     * the following integer math occurs in af_packet.c, the remainder is lost so the number
     * of frames per block MUST be either 1 or a power of 2:
     * packet_set_ring(): rb->block_frm_nr = req->tp_block_size / req->tp_frame_size;
     * packet_set_ring(): Checks if (block_frm_nr == 0) return EINVAL;
     * packet_set_ring(): checks if (block_frm_nr * tp_block_nr != tp_frame_nr) return EINVAL;
     */

    uint32_t block_frm_nr = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
    uint32_t next_power = 0;
    uint32_t is_power_of_two = (block_frm_nr != 0) && ((block_frm_nr & (block_frm_nr - 1)) == 0 );

    if ((block_frm_nr != 1) && (is_power_of_two != 1)) {

        if (etherate->app_opt.verbose)
            printf("Frames per block (%u) must be 1 or a power of 2!\n", block_frm_nr);

        next_power = block_frm_nr;
        next_power -= 1;
        next_power |= next_power >> 1;
        next_power |= next_power >> 2;
        next_power |= next_power >> 4;
        next_power |= next_power >> 8;
        next_power |= next_power >> 16;
        next_power += 1;

        etherate->frm_opt.block_sz = next_power * etherate->frm_opt.block_frm_sz;            

        if (etherate->frm_opt.block_sz % getpagesize() != 0) {
            uint32_t base = (etherate->frm_opt.block_sz / getpagesize()) + 1;
            etherate->frm_opt.block_sz = (base * getpagesize());
        }

        etherate->frm_opt.block_frm_sz = etherate->frm_opt.block_sz / next_power;
        block_frm_nr = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;

        if (etherate->app_opt.verbose) {
            printf("Frames per block increased to %d and block size increased to %u.\n",
                    block_frm_nr, etherate->frm_opt.block_sz);

            printf("Block frame size increased to %u to evenly fill block.\n",
                    etherate->frm_opt.block_frm_sz);
        }


    // If the number of frames per block is already 1 or a power of 2, the frame block size must fill the block exactly,
    // use integer math to round off the frame block size to an exact multiple of the block size:
    } else if ((etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz != 1) ||
               (etherate->frm_opt.block_sz % etherate->frm_opt.block_frm_sz != 0)) {

         uint32_t base = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
         etherate->frm_opt.block_frm_sz = etherate->frm_opt.block_sz / base;

         if (etherate->app_opt.verbose)
             printf("Block frame size increased to %u to evenly fill block.\n",
                    etherate->frm_opt.block_frm_sz);

    }


    block_frm_nr = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
    etherate->frm_opt.frame_nr = (etherate->frm_opt.block_sz * etherate->frm_opt.block_nr) / etherate->frm_opt.block_frm_sz;

    if (etherate->app_opt.verbose)
        printf("Frame block size %d, frames per block %d, block size %d, block number %d, frames in ring %d\n",
               etherate->frm_opt.block_frm_sz,
               block_frm_nr,
               etherate->frm_opt.block_sz,
               etherate->frm_opt.block_nr,
               etherate->frm_opt.frame_nr);

    if ((block_frm_nr * etherate->frm_opt.block_nr) != etherate->frm_opt.frame_nr) {
        printf("Frames per block (%d) * block number (%d) != frame number in ring (%d)!\n",
               block_frm_nr, etherate->frm_opt.block_nr, etherate->frm_opt.frame_nr);
    }

}



int32_t tpacket_v2_sock(struct thd_opt *thd_opt) {

    // Create a raw socket
    thd_opt->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock_fd == -1) {
        perror("Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Enable promiscuous mode
    int32_t sock_promisc = sock_op(S_O_PROMISC_ADD, thd_opt);

    if (sock_promisc == -1) {
        perror("Can't enable promisc mode");
        return EXIT_FAILURE;
    }


    // Bind socket to interface.
    // This is mandatory (with zero copy) to know the header size of frames
    // used in the circular buffer:
    int32_t sock_bind = sock_op(S_O_BIND, thd_opt);

    if (sock_bind == -1) {
        perror("Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Increase the socket Tx queue size so that the entire PACKET_MMAP Tx ring
    // can fit into the socket Tx queue. The Kernel will double the value provided
    // to allow for sk_buff overhead:
    int32_t sock_qlen = sock_op(S_O_QLEN_TP23, thd_opt);

    if (sock_qlen == -1) {
        return EXIT_FAILURE;
    }


    // Bypass the kernel qdisc layer and push packets directly to the driver
    /////if (thd_opt->sk_mode == SKT_TX) { ///// Check for rx speed increase

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)

            int32_t sock_qdisc = sock_op(S_O_QDISC, thd_opt);

            if (sock_qdisc == -1) {
                perror("Can't enable QDISC bypass on socket");
                return EXIT_FAILURE;
            }

        #endif
    /////}


    // Enable Tx ring to skip over malformed packets
    if (thd_opt->sk_mode == SKT_TX) {

        int32_t sock_lossy = sock_op(S_O_LOSSY, thd_opt);

        if (sock_lossy == -1) {
            perror("Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
        }
        
    }


    // Request hardware timestamping of received packets
    int32_t sock_nic_ts = sock_op(S_O_NIC_TS, thd_opt);

    if (sock_nic_ts == -1) {
        perror("Couldn't set ring timestamp source");
        // If hardware timestamps aren't support the Kernel will fall back to
        // software, no need to exit on error
    }


    // Set the socket timestamping settings:
    int32_t sock_ts = sock_op(S_O_TS, thd_opt);

    if (sock_ts == -1) {
        perror("Couldn't set socket Rx timestamp source");
        /////exit (EXIT_FAILURE); // What is the reason to exit when this fails?
    }


    // Set the TPACKET version to 2
    ///// What kernel version supports TPACKET v2?
    ///// #if LINUX_VERSION_CODE >= KERNEL_VERSION(X,XX,0)
    int32_t sock_tpk_ver = sock_op(S_O_VER_TP2, thd_opt);

    if (sock_tpk_ver == -1) {
        perror("Can't set socket tpacket version");
        return EXIT_FAILURE;
    }
    /////#else
    /////printf("TPACKETv2 required Kernel version >= XXX\n");
    /////#endif


    // Enable the Tx/Rx ring buffer
    int32_t sock_ring = sock_op(S_O_RING_TP2, thd_opt);
    
    if (sock_ring == -1) {
        perror("Can't enable Tx/Rx ring for socket");
        return EXIT_FAILURE;
    }
    

    // mmap() the Tx/Rx ring buffer against the socket
    int32_t sock_mmap = sock_op(S_O_MMAP_TP23, thd_opt); 

    if (sock_mmap == -1) { ///// Standardise these ret value checks
        perror("Ring mmap failed");
        return EXIT_FAILURE;
    }


    // Join this socket to the fanout group
    if (thd_opt->thd_cnt > 1) {

        //if (thd_opt->verbose)
        //    printf("Joining thread %d to fanout group %d...\n", thd_opt->thd_id, thd_opt->fanout_grp); ///// Add thread ID/number

        int32_t sock_fanout = sock_op(S_O_FANOUT, thd_opt);

        if (sock_fanout < 0) {
            perror("Can't configure fanout");
            return EXIT_FAILURE;
        }

    }


    return EXIT_SUCCESS;

}



void *tx_tpacket_v2(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = tpacket_v2_sock(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }
    
    struct tpacket2_hdr *hdr;
    uint8_t *data;
    uint16_t i;
    int64_t tx_bytes = 0;
    /////thd_opt->started = 1;

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

        ///// Any difference on > 4.1 kernel with real NIC?
        // I think MSG_DONTWAIT is having no affect here? Test on real NIC with NET_TX on seperate core
        tx_bytes = sendto(thd_opt->sock_fd, NULL, 0, MSG_DONTWAIT, NULL, 0);


        if (tx_bytes == -1) {
            perror("packet_mmap Tx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->tx_pkts  += (tx_bytes / thd_opt->frame_sz); ///// Replace all instances "packet"/"pkt" with "frame"/"frm"
        thd_opt->tx_bytes += tx_bytes;


    }

}

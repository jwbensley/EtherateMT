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



void *tpacket_v2_init(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;


    // Save the thread tid
    pid_t thread_id;
    thread_id = syscall(SYS_gettid);
    thd_opt->thd_id = thread_id;


    // Set the thread cancel type and register the cleanup handler
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push(thread_cleanup, thd_opt_p);

    
    if (thd_opt->verbose)
        printf("Worker thread %" PRIu32 " started\n", thd_opt->thd_id);


    tpacket_v2_ring_align(thd_opt_p);


    if (tpacket_v2_sock(thd_opt) != EXIT_SUCCESS) {
        pthread_exit((void*)EXIT_FAILURE);
    }


    if (thd_opt->sk_mode == SKT_RX) {
        tpacket_v2_rx(thd_opt_p);
    } else if (thd_opt->sk_mode == SKT_TX) {
        tpacket_v2_tx(thd_opt_p);
    } else if (thd_opt->sk_mode == SKT_BIDI) {
        printf("%" PRIu32 ":Not implemented yet!\n", thd_opt->thd_id);
        pthread_exit((void*)EXIT_FAILURE);
    }


    pthread_cleanup_pop(0);


    return NULL;

}



void tpacket_v2_ring_align(struct thd_opt *thd_opt) {

    /*
     * The frame allocation in the ring block holds the full layer 2 frame 
     * (headers + data) and some meta data, so it must hold TPACKET2_HDRLEN
     * (which is 52 bytes) aligned with TPACKET_ALIGN() (which increases it
     * from 52 to 64 bytes) + the minimum Ethernet layer 2 frame size (which
     * is 64 bytes).
     * Ensure the frame size within each block supports this minimum size:
     */

    if (thd_opt->block_frm_sz < (thd_opt->frame_sz + TPACKET_ALIGN(TPACKET2_HDRLEN)))
        thd_opt->block_frm_sz = (thd_opt->frame_sz + TPACKET_ALIGN(TPACKET2_HDRLEN));


    // In TPACKET v2 each block most hold exactly one frame:
    if (thd_opt->block_sz < thd_opt->block_frm_sz) {
        
        if (thd_opt->verbose)
            printf("%" PRIu32 ":Block size (%" PRIu32 ") is less than block frame size (%" PRIu32 "),"
                   " padding to %" PRIu32 "!\n.",
                   thd_opt->thd_id,
                   thd_opt->block_sz,
                   thd_opt->block_frm_sz,
                   thd_opt->block_frm_sz);
        
        thd_opt->block_sz = thd_opt->block_frm_sz;
    }


    // Block size must be an integer multiple of pages
    if ((thd_opt->block_sz < (uint32_t)getpagesize()) ||
        (thd_opt->block_sz % (uint32_t)getpagesize() != 0)) {

        uint32_t base = (thd_opt->block_sz / (uint32_t)getpagesize()) + 1;
        
        if (thd_opt->verbose)
            printf("%" PRIu32 ":Block size (%" PRIu32 ") is not a multiple of the page size (%" PRIu32 "),"
                   " padding to %" PRIu32 "!\n",
                   thd_opt->thd_id,
                   thd_opt->block_sz,
                   (uint32_t)getpagesize(),
                   (base * (uint32_t)getpagesize()));
        
        thd_opt->block_sz = (base * (uint32_t)getpagesize());
    }


    /*
     * The block frame size must be a multiple of TPACKET_ALIGNMENT (16),
     * also the following integer math occurs in af_packet.c, the remainder is
     * lost so the number of frames per block MUST be either 1 or a power of 2
     * (with TPACKET v2 the number of frames per block must be 1!):
     * packet_set_ring(): rb->block_frm_nr = req->tp_block_size / req->tp_frame_size;
     * packet_set_ring(): Checks if (block_frm_nr == 0) return EINVAL;
     * packet_set_ring(): checks if (block_frm_nr * tp_block_nr != tp_frame_nr) return EINVAL;
     */

    uint32_t block_frm_nr = thd_opt->block_sz / thd_opt->block_frm_sz;
    uint32_t next_power = 0;
    uint32_t is_power_of_two = (block_frm_nr != 0) && ((block_frm_nr & (block_frm_nr - 1)) == 0 );

    if ((block_frm_nr != 1) && (is_power_of_two != 1)) {

        if (thd_opt->verbose)
            printf("%" PRIu32 ":Frames per block (%" PRIu32 ") must be 1 or a power of 2!\n",
                   thd_opt->thd_id, block_frm_nr);

        next_power = block_frm_nr;   ///// Move into seperate function
        next_power -= 1;
        next_power |= next_power >> 1;
        next_power |= next_power >> 2;
        next_power |= next_power >> 4;
        next_power |= next_power >> 8;
        next_power |= next_power >> 16;
        next_power += 1;

        thd_opt->block_sz = next_power * thd_opt->block_frm_sz;            

        if (thd_opt->block_sz % (uint32_t)getpagesize() != 0) {
            uint32_t base = (thd_opt->block_sz / (uint32_t)getpagesize()) + 1;
            thd_opt->block_sz = (base * (uint32_t)getpagesize());
        }

        thd_opt->block_frm_sz = thd_opt->block_sz / next_power;
        block_frm_nr = thd_opt->block_sz / thd_opt->block_frm_sz;

        if (thd_opt->verbose) {
            printf("%" PRIu32 ":Frames per block increased to %" PRIu32 " and block size increased to %" PRIu32 ".\n",
                    thd_opt->thd_id, block_frm_nr, thd_opt->block_sz);

            printf("%" PRIu32 ":Block frame size increased to %" PRIu32 " to evenly fill block.\n",
                    thd_opt->thd_id, thd_opt->block_frm_sz);
        }


    // If the number of frames per block is already 1 or a power of 2, the frame block size must fill the block exactly,
    // use integer math to round off the frame block size to an exact multiple of the block size:
    } else if ((thd_opt->block_sz / thd_opt->block_frm_sz != 1) ||
               (thd_opt->block_sz % thd_opt->block_frm_sz != 0)) {

         uint32_t base = thd_opt->block_sz / thd_opt->block_frm_sz;
         thd_opt->block_frm_sz = thd_opt->block_sz / base;

         if (thd_opt->verbose)
             printf("%" PRIu32 ":Block frame size increased to %" PRIu32 " to evenly fill block.\n",
                    thd_opt->thd_id, thd_opt->block_frm_sz);

    }


    block_frm_nr = thd_opt->block_sz / thd_opt->block_frm_sz;
    thd_opt->frame_nr = (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frm_sz;

    if (thd_opt->verbose)
        printf("%" PRIu32 ":Block frame size %" PRIu32 ", frames per block %" PRIu32 ","
               " block size %" PRIu32 ", block number %" PRIu32 ", frames in ring %" PRIu32 "\n",
               thd_opt->thd_id,
               thd_opt->block_frm_sz,
               block_frm_nr,
               thd_opt->block_sz,
               thd_opt->block_nr,
               thd_opt->frame_nr);


    if ((block_frm_nr * thd_opt->block_nr) != thd_opt->frame_nr) {
        printf("%" PRIu32 ":Frames per block (%" PRIu32 ") * block number (%" PRIu32 ")"
               " != frame number in ring (%" PRIu32 ")!\n",
               thd_opt->thd_id, block_frm_nr, thd_opt->block_nr, thd_opt->frame_nr);
    }

}



void tpacket_v2_ring_init(struct thd_opt *thd_opt) {

    struct tpacket_req *tpacket_req  = thd_opt->tpacket_req;

    tpacket_req->tp_block_size = thd_opt->block_sz;
    tpacket_req->tp_frame_size = thd_opt->block_frm_sz; // tp_frame_size = TPACKET2_HDRLEN + frame_sz
    tpacket_req->tp_block_nr   = thd_opt->block_nr;
    tpacket_req->tp_frame_nr   = thd_opt->frame_nr;

}



void tpacket_v2_rx(struct thd_opt *thd_opt) {


    /*
        1. Replace struct tpacket_hdr by struct tpacket2_hdr
        2. Query header len and save
        3. Set protocol version to 2, set up ring as usual
        4. For getting the sockaddr_ll,
           use (void *)hdr + TPACKET_ALIGN(hdrlen) instead of
           (void *)hdr + TPACKET_ALIGN(sizeof(struct tpacket_hdr))
    */

    thd_opt->started = 1;

    /*
    struct tpacket2_hdr *hdr;

    while (1) {

        for (uint32_t i = 0; i < thd_opt->block_nr; i++) {

            hdr = (void*)(thd_opt->mmap_buf + (thd_opt->block_sz * i));

            if ((hdr->tp_status & TP_STATUS_USER) == TP_STATUS_USER) {
                
                thd_opt->rx_frms += 1;
                thd_opt->rx_bytes += hdr->tp_snaplen;

                hdr->tp_status = 0;
                __sync_synchronize();

            }
        }

    }
    */

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = thd_opt->sock;
    pfd.events = POLLIN | POLLERR;
    pfd.revents = 0;

    uint32_t blk_num = 0;
    struct tpacket2_hdr *hdr = NULL;

    while(1) {

        if (poll(&pfd, 1, -1) == -1) {
            tperror(thd_opt, "Rx poll error");
            pthread_exit((void*)EXIT_FAILURE);
        }

        if (pfd.revents != POLLIN)
            printf("%" PRIu32 ":Unexpected poll event %" PRId32 "\n", thd_opt->thd_id, pfd.revents);

        hdr = (void*)(thd_opt->mmap_buf + (thd_opt->block_frm_sz * blk_num));

        if (!(hdr->tp_status & TP_STATUS_USER))
            printf("%" PRIu32 ":No data in ring block, status %" PRIu32 "\n", thd_opt->thd_id, hdr->tp_status);

        thd_opt->rx_frms += 1;
        thd_opt->rx_bytes += hdr->tp_snaplen;

        // Reset the block stats back to KERNEL (userland is finished with it)
        hdr->tp_status = TP_STATUS_KERNEL;

        blk_num = (blk_num + 1) % thd_opt->block_nr;

    }

}


int32_t tpacket_v2_sock(struct thd_opt *thd_opt) {

    // TPACKET_V2 ring settings
    struct tpacket_req tpacket_req;
    memset(&tpacket_req, 0, sizeof(tpacket_req));
    thd_opt->tpacket_req = (void*)&tpacket_req;
    thd_opt->tpacket_req_sz = sizeof(struct tpacket_req);

    thd_opt->tpacket_ver = TPACKET_V2;

    if (thd_opt->sk_mode == SKT_RX) {
        thd_opt->ring_type = PACKET_RX_RING;
    } else {
        thd_opt->ring_type = PACKET_TX_RING;
    }


    // Create a raw socket
    thd_opt->sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock == -1) {
        tperror(thd_opt, "Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bind socket to interface,
    // this is mandatory (with zero copy) to know the header size of frames
    // used in the circular buffer
    if (sock_op(S_O_BIND, thd_opt) == -1) {
        tperror(thd_opt, "Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bypass the kernel qdisc layer and push frames directly to the driver
    if (sock_op(S_O_QDISC, thd_opt) == -1) {
        tperror(thd_opt, "Can't enable QDISC bypass on socket");
        return EXIT_FAILURE;
    }


    // Enable Tx ring to skip over malformed frames
    if (thd_opt->sk_mode == SKT_TX) {

        if (sock_op(S_O_LOSSY, thd_opt) == -1) {
            tperror(thd_opt, "Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
        }
        
    }


    // Set the socket Rx timestamping settings
    if (sock_op(S_O_TS, thd_opt) == -1) {
        tperror(thd_opt, "Can't set socket Rx timestamp source");
    }


    // Join this socket to the fanout group
    if (thd_opt->thd_nr > 1) {

        if (sock_op(S_O_FANOUT, thd_opt) < 0) {
            tperror(thd_opt, "Can't configure socket fanout");
            return EXIT_FAILURE;
        } else {
            if (thd_opt->verbose)
                printf("%" PRIu32 ":Joint fanout group %" PRIu32 "\n",
                       thd_opt->thd_id, thd_opt->fanout_grp);
        }

    }


    // Increase the socket Tx/Rx queue size so that the entire PACKET_MMAP ring
    // can fit into the socket Tx queue. The Kernel will double the value provided
    // to allow for sk_buff overhead:
    if (sock_op(S_O_QLEN, thd_opt) == -1) {
        tperror(thd_opt, "Can't change the socket Tx queue length");
        return EXIT_FAILURE;
    }


    // Request hardware timestamping of received packets. If hardware
    // timestamps aren't supported the Kernel will fall back to software
    if (sock_op(S_O_NIC_TS, thd_opt) == -1) {
        tperror(thd_opt, "Can't set ring timestamp source");
    }


    // Set the TPACKET version to 2
    if (sock_op(S_O_VER_TP, thd_opt) == -1) {
        tperror(thd_opt, "Can't set socket tpacket version");
        return EXIT_FAILURE;
    }


    // Enable the Tx/Rx ring buffer
    tpacket_v2_ring_init(thd_opt);
    if (sock_op(S_O_RING_TP2, thd_opt) == -1) {
        tperror(thd_opt, "Can't enable Tx/Rx ring for socket");
        return EXIT_FAILURE;
    }
    

    // mmap() the Tx/Rx ring buffer against the socket
    if (sock_op(S_O_MMAP_TP23, thd_opt) == -1) {
        tperror(thd_opt, "Can't mmap ring failed");
        return EXIT_FAILURE;
    }


    // Allocate an iovec for each frame block within the ring
    // (one frame per block in TPACKET v2)
    thd_opt->ring = (struct iovec*)calloc(tpacket_req.tp_frame_nr * sizeof(struct iovec), 1);
    if (thd_opt->ring == NULL) {
        tperror(thd_opt, "Can't calloc ring buffer");
        return EXIT_FAILURE;
    }
    for (uint16_t i = 0; i < tpacket_req.tp_frame_nr; ++i) {
        thd_opt->ring[i].iov_base = thd_opt->mmap_buf + (i * tpacket_req.tp_frame_size);
        thd_opt->ring[i].iov_len  = tpacket_req.tp_frame_size;
    }


    return EXIT_SUCCESS;

}



void tpacket_v2_stats(struct thd_opt *thd_opt, uint64_t *rx_drops) {

    struct tpacket_stats tp_stats;  // tp_drops is only incremented by the Kernel on Rx, not Tx
    memset(&tp_stats, 0, sizeof(tp_stats));

    socklen_t stats_len = sizeof(tp_stats);
    int32_t sock_stats = getsockopt(thd_opt->sock, SOL_PACKET, PACKET_STATISTICS, &tp_stats, &stats_len);

    if (sock_stats < 0) {
        tperror(thd_opt, "Couldn't get TPACKET V2 Rx socket stats");
        pthread_exit((void*)EXIT_FAILURE);
    }

    *rx_drops += tp_stats.tp_drops;

}



void tpacket_v2_tx(struct thd_opt *thd_opt) {
    
    struct tpacket2_hdr *hdr;
    uint8_t *data;
    uint32_t i;
    int64_t tx_bytes = 0;
    
    thd_opt->started = 1;

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
        tx_bytes = sendto(thd_opt->sock, NULL, 0, MSG_DONTWAIT, NULL, 0);


        if (tx_bytes == -1) {

            if (errno != ENOBUFS) {
                tperror(thd_opt, "PACKET_MMAP Tx error");
                pthread_exit((void*)EXIT_FAILURE);
            } else {
                thd_opt->stalling = 1;
            }
        
        }
 
        thd_opt->tx_frms  += (tx_bytes / thd_opt->frame_sz);
        thd_opt->tx_bytes += tx_bytes;
 
    }

}

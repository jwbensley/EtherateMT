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



#include "tpacket_v3.h"



void *tpacket_v3_init(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;


    // Save the thread tid
    pid_t thread_id;
    thread_id = syscall(SYS_gettid);
    thd_opt->thd_id = thread_id;


    // Set the thread cancel type and register the cleanup handler
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push(thd_cleanup, thd_opt_p);
    

    if (thd_opt->verbose)
        printf("Worker thread %" PRIu32 " started\n", thd_opt->thd_id);


    tpacket_v3_ring_align(thd_opt_p);


    if (tpacket_v3_sock(thd_opt) != EXIT_SUCCESS) {
        pthread_exit((void*)EXIT_FAILURE);
    }


    if (thd_opt->sk_mode == SKT_RX) {
        tpacket_v3_rx(thd_opt_p);

    } else if (thd_opt->sk_mode == SKT_TX) {
        // af_packet.c: packet_set_ring()
        /* Opening a Tx-ring is NOT supported in TPACKET_V3 */
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
        tpacket_v3_tx(thd_opt_p);
        #else
        uint32_t version     = (LINUX_VERSION_CODE >> 16);
        uint32_t patch_level = (LINUX_VERSION_CODE & 0xffff) >> 8;
        uint32_t sub_level   = (LINUX_VERSION_CODE & 0xff);
        printf("Kernel version detected as %" PRIu32 ".%" PRIu32 ".%" PRIu32 ","
               "TPACKET_V3 with PACKET_TX_RING requires 4.11.\n",
               version, patch_level, sub_level);
        pthread_exit((void*)EXIT_FAILURE);
        #endif
        
    } else if (thd_opt->sk_mode == SKT_BIDI) {
        printf("%" PRIu32 ":Not implemented yet!\n", thd_opt->thd_id);
        pthread_exit((void*)EXIT_FAILURE);
    }


    pthread_cleanup_pop(0);

    
    return NULL;

}



void tpacket_v3_ring_align(struct thd_opt *thd_opt) {


    /*
     * The frame allocation in the ring block holds the full layer 2 frame 
     * (headers + data) and some meta data, so it must hold TPACKET2_HDRLEN    //// Update for v3
     * (which is 52 bytes) aligned with TPACKET_ALIGN() (which increases it
     * from 52 to 64 bytes) + the minimum Ethernet layer 2 frame size (which
     * is 64 bytes):
     */
    thd_opt->block_frm_sz = (thd_opt->frame_sz + TPACKET_ALIGN(TPACKET3_HDRLEN));


    // Blocks must contain at least 1 frame because frames can not be fragmented across blocks
    if (thd_opt->block_sz < thd_opt->block_frm_sz) {
        
        if (thd_opt->verbose) {
            printf("%" PRIu32 ":Block size (%" PRIu32 ") is less than block frame size (%" PRIu32 "),"
                   " padding to %" PRIu32 "!\n.",
                   thd_opt->thd_id,
                   thd_opt->block_sz,
                   thd_opt->block_frm_sz,
                   thd_opt->block_frm_sz);
        }
        
        thd_opt->block_sz = thd_opt->block_frm_sz;
    }

    // Block size must be an integer multiple of pages
    if ( (thd_opt->block_sz < (uint32_t)getpagesize()) ||
         (thd_opt->block_sz % (uint32_t)getpagesize() != 0)) {

        uint32_t base = (thd_opt->block_sz / (uint32_t)getpagesize()) + 1;
        
        if (thd_opt->verbose) {
            printf("%" PRIu32 ":Block size (%" PRIu32 ") is not a multiple of the page size (%" PRIu32 "),"
                   " padding to %" PRIu32 "!\n",
                   thd_opt->thd_id,
                   thd_opt->block_sz,
                   (uint32_t)getpagesize(),
                   (base * (uint32_t)getpagesize()));
        }
        
        thd_opt->block_sz = (base * (uint32_t)getpagesize());
    }


    /*
     * The block frame size must be a multiple of TPACKET_ALIGNMENT (16) also,
     * the following integer math occurs in af_packet.c, the remainder is lost so the number
     * of frames per block MUST be either 1 or a power of 2:
     * packet_set_ring(): rb->block_frm_nr = req->tp_block_size / req->tp_frame_size;
     * packet_set_ring(): Checks if (block_frm_nr == 0) return EINVAL;
     * packet_set_ring(): checks if (block_frm_nr * tp_block_nr != tp_frame_nr) return EINVAL;
     */


    ///// Add these checks from packet_set_ring()

    /*
    req->tp_block_size <= BLK_PLUS_PRIV((u64)req_u->req3.tp_sizeof_priv)

    */


    uint32_t block_frm_nr = thd_opt->block_sz / thd_opt->block_frm_sz;
    uint32_t next_power = 0;
    uint32_t is_power_of_two = (block_frm_nr != 0) && ((block_frm_nr & (block_frm_nr - 1)) == 0 );

    if ((block_frm_nr != 1) && (is_power_of_two != 1)) {

        if (thd_opt->verbose) {
            printf("%" PRIu32 ":Frames per block (%" PRIu32 ") must be 1 or a power of 2!\n",
                   thd_opt->thd_id,
                   block_frm_nr);
        }

        next_power = block_frm_nr; ///// Move into seperate function
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
    } else if (
        (thd_opt->block_sz / thd_opt->block_frm_sz != 1) ||
        (thd_opt->block_sz % thd_opt->block_frm_sz != 0)) {

         uint32_t base = thd_opt->block_sz / thd_opt->block_frm_sz;
         thd_opt->block_frm_sz = thd_opt->block_sz / base;

         if (thd_opt->verbose) {
             printf("%" PRIu32 ":Block frame size increased to %" PRIu32 " to evenly fill block.\n",
                    thd_opt->thd_id, thd_opt->block_frm_sz);
         }
    }


    block_frm_nr = thd_opt->block_sz / thd_opt->block_frm_sz;
    thd_opt->frame_nr = (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frm_sz;
    if (thd_opt->verbose) {
        printf("%" PRIu32 ":Block frame size %" PRIu32 ", frames per block %" PRIu32 ","
               " block size %" PRIu32 ", block number %" PRIu32 ", frames in ring %" PRIu32 "\n",
               thd_opt->thd_id,
               thd_opt->block_frm_sz,
               block_frm_nr,
               thd_opt->block_sz,
               thd_opt->block_nr,
               thd_opt->frame_nr);
    }

    if ((block_frm_nr * thd_opt->block_nr) != thd_opt->frame_nr) {
        printf("%" PRIu32 ":Frames per block (%" PRIu32 ") * block number (%" PRIu32 ") "
               "!= frame number in ring (%" PRIu32 ")!\n",
               thd_opt->thd_id, block_frm_nr, thd_opt->block_nr, thd_opt->frame_nr);
    }

}



void tpacket_v3_ring_init(struct thd_opt *thd_opt) {

    struct  tpacket_req3 *tpacket_req3 = thd_opt->tpacket_req3;

    tpacket_req3->tp_block_size = thd_opt->block_sz;
    tpacket_req3->tp_frame_size = thd_opt->block_frm_sz;
    tpacket_req3->tp_block_nr   = thd_opt->block_nr;
    tpacket_req3->tp_frame_nr   = thd_opt->frame_nr; // (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frm_sz;
    tpacket_req3->tp_retire_blk_tov   = 0; ////// Timeout in msec, what does this do? -timeout after which a block is retired, even if it’s not fully filled with data (see below).
    tpacket_req3->tp_feature_req_word = 0; //TP_FT_REQ_FILL_RXHASH;  ///// What does this do? - the size of per-block private area. This area can be used by a user to store arbitrary information associated with each block.
    tpacket_req3->tp_sizeof_priv      = 0; ///// What does this do? -  a set of flags (actually just one at the moment), which allows to enable some additional functionality.

    /* af_packet.c requires tp_retire_blk_tov, tp_feature_req_word and tp_sizeof_priv = 0:

    packet_set_ring() {
    ...
    case TPACKET_V3:
        // Block transmit is not supported yet
        if (!tx_ring) {
            init_prb_bdqc(po, rb, pg_vec, req_u);
        } else {
            struct tpacket_req3 *req3 = &req_u->req3;

            if (req3->tp_retire_blk_tov ||
                req3->tp_sizeof_priv ||
                req3->tp_feature_req_word) {
                err = -EINVAL;
                goto out;
            }
        }
    }
    */

}



void tpacket_v3_rx(struct thd_opt *thd_opt) {

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = thd_opt->sock;
    pfd.events = POLLIN | POLLERR;
    pfd.revents = 0;


    uint32_t blk_num = 0;
    struct block_desc *pbd = NULL;

    thd_opt->started = 1;
    
    while (1) {
        pbd = (struct block_desc *) thd_opt->ring[blk_num].iov_base;
 
        if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
            int32_t poll_ret = poll(&pfd, 1, -1);

            if (poll_ret == -1) {
                ///// TODO
                /////tperror(thd_opt, "Rx poll error");
                /////pthread_exit((void*)EXIT_FAILURE);
                thd_opt->sk_err += 1;
            }

            if (pfd.revents != POLLIN)
                printf("Unexpected poll event (%d)", pfd.revents);

        }


        uint32_t num_frms = pbd->h1.num_pkts;
        uint32_t bytes = 0;
        struct tpacket3_hdr *ppd;

        ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd + pbd->h1.offset_to_first_pkt);

        for (uint32_t i = 0; i < num_frms; ++i) {
            bytes += ppd->tp_snaplen;

            ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd + ppd->tp_next_offset);
        }


        thd_opt->rx_frms += num_frms;
        thd_opt->rx_bytes += bytes;

        // Reset the block stats back to KERNEL (userland is finished with it)
        pbd->h1.block_status = TP_STATUS_KERNEL;

        blk_num = (blk_num + 1) % thd_opt->block_nr;
    }   


}



int32_t tpacket_v3_sock(struct thd_opt *thd_opt) {

    // TPACKET_V3 ring settings
    struct  tpacket_req3 tpacket_req3;
    memset(&tpacket_req3, 0, sizeof(tpacket_req3));
    thd_opt->tpacket_req3 = &tpacket_req3;
    thd_opt->tpacket_req3_sz = sizeof(struct tpacket_req3);

    thd_opt->tpacket_ver = TPACKET_V3;

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


    // Set the TPACKET version to 3
    if (sock_op(S_O_VER_TP, thd_opt) == -1) {
        tperror(thd_opt, "Can't set socket tpacket version");
        return EXIT_FAILURE;
    }

    
    // Enable the Tx/Rx ring buffer
    tpacket_v3_ring_init(thd_opt);
    if (sock_op(S_O_RING_TP3, thd_opt) == -1) {
        tperror(thd_opt, "Can't enable Tx/Rx ring for socket");
        return EXIT_FAILURE;
    }
    

    // mmap() the Tx/Rx ring buffer against the socket
    if (sock_op(S_O_MMAP_TP23, thd_opt) == -1) {
        tperror(thd_opt, "Can't mmap ring");
        return EXIT_FAILURE;
    }


    // Allocate an iovec for each block within the ring
    // Each block can hold multiple frames in TPACKET_V3)
    thd_opt->ring = (struct iovec*)calloc(tpacket_req3.tp_block_nr * sizeof(struct iovec), 1);
    if (thd_opt->ring == NULL) {
        tperror(thd_opt, "Can't calloc ring buffer");
        return EXIT_FAILURE;
    }
    for (uint16_t i = 0; i < tpacket_req3.tp_block_nr; ++i) {
        thd_opt->ring[i].iov_base = thd_opt->mmap_buf + (i * tpacket_req3.tp_block_size);
        thd_opt->ring[i].iov_len  = tpacket_req3.tp_block_size;
    }


    return EXIT_SUCCESS;

}



void tpacket_v3_stats(struct thd_opt *thd_opt, uint64_t *rx_drops, uint64_t *rx_qfrz) {

    struct tpacket_stats_v3 tp3_stats;
    memset(&tp3_stats, 0, sizeof(tp3_stats));

    socklen_t stats_len = sizeof(tp3_stats);
    int32_t sock_stats = getsockopt(thd_opt->sock, SOL_PACKET, PACKET_STATISTICS, &tp3_stats, &stats_len);

    if (sock_stats < 0) {
        tperror(thd_opt, "Couldn't get TPACKET V3 Rx socket stats");
        pthread_exit((void*)EXIT_FAILURE);
    }

    *rx_drops += tp3_stats.tp_drops;
    *rx_qfrz  += tp3_stats.tp_freeze_q_cnt;

}



void tpacket_v3_tx(struct thd_opt *thd_opt) {
    
    struct tpacket3_hdr *hdr;
    uint8_t *data;
    uint32_t i;
    int64_t ret = 0;
    
    thd_opt->started = 1;

    /*
    TPACKET_V2 --> TPACKET_V3:
        - Flexible buffer implementation for RX_RING:
            1. Blocks can be configured with non-static frame-size
            2. Read/poll is at a block-level (as opposed to packet-level)
            3. Added poll timeout to avoid indefinite user-space wait
               on idle links
            4. Added user-configurable knobs:
                4.1 block::timeout
                4.2 tpkt_hdr::sk_rxhash
        - RX Hash data available in user space
        - TX_RING semantics are conceptually similar to TPACKET_V2;
          use tpacket3_hdr instead of tpacket2_hdr, and TPACKET3_HDRLEN
          instead of TPACKET2_HDRLEN. In the current implementation,
          the tp_next_offset field in the tpacket3_hdr MUST be set to
          zero, indicating that the ring does not hold variable sized frames.
          Packets with non-zero values of tp_next_offset will be dropped.
    */

    
    while(1) {


        for (i = 0; i < thd_opt->frame_nr; i += 1) {
            hdr = (void*)(thd_opt->mmap_buf + (thd_opt->block_frm_sz * i));
            // TPACKET2_HDRLEN == (TPACKET_ALIGN(sizeof(struct tpacket2_hdr)) + sizeof(struct sockaddr_ll))   ///// Update for TPACKET V3
            // For raw Ethernet frames where the layer 2 headers are present
            // and the ring blocks are already aligned its fine to use:
            // sizeof(struct tpacket2_hdr)
            data = (uint8_t*)hdr + sizeof(struct tpacket3_hdr);
            memcpy(data, thd_opt->tx_buffer, thd_opt->frame_sz);
            hdr->tp_len = thd_opt->frame_sz;
            hdr->tp_status = TP_STATUS_SEND_REQUEST;
            
        }

        //ret = sendto(thd_opt->sock, NULL, 0, 0, NULL, 0);
        ret = send(thd_opt->sock, NULL, 0, 0);


        if (ret == -1) {
            thd_opt->sk_err += 1;
        }

        for (i = 0; i < thd_opt->frame_nr; i += 1) {
            hdr = (void*)(thd_opt->mmap_buf + (thd_opt->block_frm_sz * i));
            if (hdr->tp_status == TP_STATUS_AVAILABLE) {
                thd_opt->tx_frms += 1;
                thd_opt->tx_bytes += thd_opt->frame_sz;
            }
            
        }


    }

}

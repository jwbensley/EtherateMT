/*
 * License: MIT
 *
 * Copyright (c) 2016-2017 James Bensley.
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



#include "packet_mmap.h"



void *packet_mmap_rx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    /////printf("This is thread %d\n", gettid());

    int32_t sk_setup_ret = packet_mmap_setup_socket(thd_opt_p);
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



void packet_mmap_setup_ring(struct etherate *etherate) {


    if (etherate->app_opt.sk_mode == SKT_TX) {

        // The frame allocation in the ring block holds the full layer 2 frame 
        // (headers + data) and some meta data, so it must hold TPACKET2_HDRLEN
        // (which is 52 bytes) aligned with TPACKET_ALIGN() (which increases it
        // from 52 to 64 bytes) + the minimum Ethernet layer 2 frame size (which
        // is 64 bytes):
        etherate->frm_opt.block_frm_sz = (etherate->frm_opt.frame_sz + TPACKET_ALIGN(TPACKET2_HDRLEN));

    } else {

        // Rx uses TPACKET v3
        etherate->frm_opt.block_frm_sz = (etherate->frm_opt.frame_sz + TPACKET_ALIGN(TPACKET3_HDRLEN));
    }


    // Blocks must contain at least 1 frame because frames can not be fragmented across blocks
    if (etherate->frm_opt.block_sz < etherate->frm_opt.block_frm_sz) {
        
        if (etherate->app_opt.verbose) {
            printf("Block size (%d) is less than block frame size (%d), padding to %d!\n.",
                   etherate->frm_opt.block_sz,
                   etherate->frm_opt.block_frm_sz,
                   etherate->frm_opt.block_frm_sz);
        }
        
        etherate->frm_opt.block_sz = etherate->frm_opt.block_frm_sz;
    }

    // Block size must be an integer multiple of pages
    if (
        (etherate->frm_opt.block_sz < getpagesize()) ||
        (etherate->frm_opt.block_sz % getpagesize() != 0)) {

        uint32_t base = (etherate->frm_opt.block_sz / getpagesize()) + 1;
        
        if (etherate->app_opt.verbose) {
            printf("Block size (%d) is not a multiple of the page size (%d), padding to %d!\n",
                    etherate->frm_opt.block_sz,
                    getpagesize(),
                    (base * getpagesize()));
        }
        
        etherate->frm_opt.block_sz = (base * getpagesize());
    }


    // The block frame size must be a multiple of TPACKET_ALIGNMENT (16) also,
    // the following integer math occurs in af_packet.c, the remainder is lost so the number
    // of frames per block MUST be either 1 or a power of 2:
    // packet_set_ring(): rb->block_frm_nr = req->tp_block_size / req->tp_frame_size;
    // packet_set_ring(): Checks if (block_frm_nr == 0) return EINVAL;
    // packet_set_ring(): checks if (block_frm_nr * tp_block_nr != tp_frame_nr) return EINVAL;
    uint32_t block_frm_nr = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
    uint32_t next_power = 0;
    uint32_t is_power_of_two = (block_frm_nr != 0) && ((block_frm_nr & (block_frm_nr - 1)) == 0 );

    if ((block_frm_nr != 1) && (is_power_of_two != 1)) {

        if (etherate->app_opt.verbose) {
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
    } else if (
        (etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz != 1) ||
        (etherate->frm_opt.block_sz % etherate->frm_opt.block_frm_sz != 0)) {

         uint32_t base = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
         etherate->frm_opt.block_frm_sz = etherate->frm_opt.block_sz / base;

         if (etherate->app_opt.verbose) {
             printf("Block frame size increased to %u to evenly fill block.\n",
                    etherate->frm_opt.block_frm_sz);
         }
    }


    block_frm_nr = etherate->frm_opt.block_sz / etherate->frm_opt.block_frm_sz;
    etherate->frm_opt.frame_nr = (etherate->frm_opt.block_sz * etherate->frm_opt.block_nr) / etherate->frm_opt.block_frm_sz;
    if (etherate->app_opt.verbose) {
        printf("Frame block size %d, frames per block %d, block size %d, block number %d, frames in ring %d\n",
               etherate->frm_opt.block_frm_sz,
               block_frm_nr,
               etherate->frm_opt.block_sz,
               etherate->frm_opt.block_nr,
               etherate->frm_opt.frame_nr);
    }

    if ((block_frm_nr * etherate->frm_opt.block_nr) != etherate->frm_opt.frame_nr) {
        printf("Frames per block (%d) * block number (%d) != frame number in ring (%d)!\n",
               block_frm_nr, etherate->frm_opt.block_nr, etherate->frm_opt.frame_nr);
    }

}



int32_t packet_mmap_setup_socket(struct thd_opt *thd_opt) {

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

    int32_t sock_bind = bind(thd_opt->sock_fd, (struct sockaddr *)&thd_opt->bind_addr,
                             sizeof(thd_opt->bind_addr));

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

        if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur,
                       &read_len) < 0) {

            perror("Can't get the socket write buffer size");
            return EXIT_FAILURE;
        }

        int32_t sock_wmem = (thd_opt->block_sz * thd_opt->block_nr);

        if (sock_wmem_cur < sock_wmem) {

            /////// Add verbose if?
            printf("Current socket write buffer size is %d bytes, "
                   "desired write buffer size is %d bytes\n",
                   sock_wmem_cur, sock_wmem);

            printf("Trying to increase socket write buffer size to %d bytes...\n",
                   sock_wmem);

            if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem,
                           sizeof(sock_wmem)) < 0) {

                perror("Can't set the socket write buffer size");
                return EXIT_FAILURE;
            }
            
            if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur,
                           &read_len) < 0) {

                perror("Can't get the socket write buffer size");
                return EXIT_FAILURE;
            }

            printf("Write buffer size set to %d bytes\n", sock_wmem_cur);


            if (sock_wmem_cur != sock_wmem) {

                printf("Write buffer still too small!\n"
                       "Trying to force the write buffer size to %d bytes...\n",
                       sock_wmem);
                
                if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUFFORCE,
                               &sock_wmem, sizeof(sock_wmem))<0) {

                    perror("Can't force the socket write buffer size");
                    return EXIT_FAILURE;
                }
                
                if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF,
                               &sock_wmem_cur, &read_len) < 0) {

                    perror("Can't get the socket write buffer size");
                    return EXIT_FAILURE;
                  }
                
                // When the buffer size is forced the kernel sets a value double
                // the requested size to allow for accounting/meta data space
                printf("Forced write buffer size is now %d bytes\n", (sock_wmem_cur/2));

                if (sock_wmem_cur < sock_wmem) {
                    printf("Still smaller than desired!\n");
                }


            }

        }


    // Increase the socket read queue size, the same as above for Tx
    } else if (thd_opt->sk_mode == SKT_RX) {


        int32_t sock_rmem_cur;
        socklen_t read_len = sizeof(sock_rmem_cur);

        if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_RCVBUF, &sock_rmem_cur,
                       &read_len) < 0) {

            perror("Can't get the socket read buffer size");
            return EXIT_FAILURE;
        }

        int32_t sock_rmem = (thd_opt->block_sz * thd_opt->block_nr);

        if (sock_rmem_cur < sock_rmem) {

            /////// Add verbose if?
            printf("Current socket read buffer size is %d bytes, "
                   "desired read buffer size is %d bytes\n",
                   sock_rmem_cur, sock_rmem);

            printf("Trying to increase socket read buffer size to %d bytes...\n",
                   sock_rmem);

            if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_rmem,
                           sizeof(sock_rmem)) < 0) {

                perror("Can't set the socket read buffer size");
                return EXIT_FAILURE;
            }
            
            if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_rmem_cur,
                           &read_len) < 0) {

                perror("Can't get the socket read buffer size");
                return EXIT_FAILURE;
            }

            printf("Read buffer size set to %d bytes\n", sock_rmem_cur);


            if (sock_rmem_cur != sock_rmem) {

                printf("Read buffer still too small!\n"
                       "Trying to force the read buffer size to %d bytes...\n",
                       sock_rmem);
                
                if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUFFORCE,
                               &sock_rmem, sizeof(sock_rmem))<0) {

                    perror("Can't force the socket read buffer size");
                    return EXIT_FAILURE;
                }
                
                if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF,
                               &sock_rmem_cur, &read_len) < 0) {

                    perror("Can't get the socket read buffer size");
                    return EXIT_FAILURE;
                  }
                
                // When the buffer size is forced the kernel sets a value double
                // the requested size to allow for accounting/meta data space
                printf("Forced read buffer size is now %d bytes\n", (sock_rmem_cur/2));

                if (sock_rmem_cur < sock_rmem) {
                    printf("Still smaller than desired!\n");
                }


            }

        }

    }



    // Bypass the kernel qdisc layer and push packets directly to the driver,
    // (packet are not buffered, tc disciplines are ignored, Tx support only).
    // This was added in Linux 3.14.
    /////if (thd_opt->sk_mode == SKT_TX) {

        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
            
            static const int32_t sock_qdisc_bypass = 1;
            int32_t sock_qdisc_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));

            if (sock_qdisc_ret == -1) {
                perror("Can't enable QDISC bypass on socket");
                return EXIT_FAILURE;
            }

        #endif
    /////}



    // Enable Tx ring to skip over malformed packets
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

    

    // Request hardware timestamping
    /*
    PACKET_TIMESTAMP (with PACKET_RX_RING; since Linux 2.6.36)
    The packet receive ring always stores a timestamp in the
    metadata header.  By default, this is a software generated
    timestamp generated when the packet is copied into the ring
    
    For Tx ring by default no timestamp is generated.
    For the Rx ring if neither TP_STATUS_TS_RAW_HARDWARE or TP_STATUS_TS_SOFTWARE
    are set a software fallback is invoked *within* PF_PACKET's processing code
    (less precise) if no hardware timestamp is enabled.
    Try to off load the Rx timestamp to hardware.
    */
    printf("Trying to offload Rx timestamps to hardware...\n");

    // Set the device/hardware timestamping settings:
    struct hwtstamp_config hwconfig;
    memset (&hwconfig, 0, sizeof(struct hwtstamp_config));
    hwconfig.tx_type = HWTSTAMP_TX_OFF;            // Disable all Tx timestamping
    hwconfig.rx_filter = HWTSTAMP_FILTER_NONE;     // Filter all Rx timestamping

    struct ifreq ifr;
    memset (&ifr, 0, sizeof(struct ifreq));
    strncpy (ifr.ifr_name, (char*)thd_opt->if_name, IF_NAMESIZE);
    ifr.ifr_data = (void *) &hwconfig;

    int32_t nic_tw_ret = ioctl(thd_opt->sock_fd, SIOCSHWTSTAMP, &ifr);
    if (nic_tw_ret < 0) {
        perror("Couldn't set hardware timestamp source");
        // If hardware timestamps aren't support the Kernel will fall back to software
    }

    // Set the socket timestamping settings:
    int32_t timesource = 0;
    timesource |= SOF_TIMESTAMPING_RX_HARDWARE;    // Set Rx timestamps to hardware
    timesource |= SOF_TIMESTAMPING_RAW_HARDWARE;   // Use hardware time stamps for reporting
    //timesource |= SOF_TIMESTAMPING_SYS_HARDWARE; // deprecated

    int32_t sock_ts_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TIMESTAMP, &timesource, sizeof(timesource));
    if (sock_ts_ret < 0) {
        perror("Couldn't set socket timestamp source");
        exit (EXIT_FAILURE);
    }



    // Enable the Tx/Rx ring buffers,
    // TPACKETV3 for Rx and V2 for Tx
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
    


    // mmap() the Tx/Rx ring buffers against the socket
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
    if (thd_opt->thd_cnt > 1) {

        uint16_t fanout_type = PACKET_FANOUT_CPU; 
        uint32_t fanout_arg = (thd_opt->fanout_grp | (fanout_type << 16));
        int32_t  sock_fan_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg));

        if (sock_fan_ret < 0) {
            perror("Can't configure fanout");
            return EXIT_FAILURE;
        }

    }


    return EXIT_SUCCESS;

}



void *packet_mmap_tx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = packet_mmap_setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }
    
    struct tpacket2_hdr *hdr;
    uint8_t *data;
    uint16_t i;
    uint64_t tx_bytes = 0;
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


        if (tx_bytes < 0) {
            perror("packet_mmap Tx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->tx_pkts  += (tx_bytes / thd_opt->frame_sz); ///// Replace all instances "packet"/"pkt" with "frame"/"frm"
        thd_opt->tx_bytes += tx_bytes;


    }

}

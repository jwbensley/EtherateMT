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



#include "sock_op.h"


int32_t sock_op(uint8_t op, struct thd_opt *thd_opt) {

    switch(op) {

        // Enable promiscuous mode on interface
        case S_O_PROMISC_ADD:

            ;
            struct packet_mreq packet_mreq;
            memset(&packet_mreq, 0, sizeof(packet_mreq));
            packet_mreq.mr_type    = PACKET_MR_PROMISC;
            packet_mreq.mr_ifindex = thd_opt->if_index;
            
            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (void *)&packet_mreq, sizeof(packet_mreq));


        // Remove promisc mode from interface
        case S_O_PROMISC_REM:

            ///// TODO    


        // Bind socket to interface
        case S_O_BIND:

            memset(&thd_opt->bind_addr, 0, sizeof(thd_opt->bind_addr));
            thd_opt->bind_addr.sll_family   = AF_PACKET;
            thd_opt->bind_addr.sll_protocol = htons(ETH_P_ALL);
            thd_opt->bind_addr.sll_ifindex  = thd_opt->if_index;

            return bind(thd_opt->sock_fd, (struct sockaddr *)&thd_opt->bind_addr,
                                     sizeof(thd_opt->bind_addr));


        // Set the socket Tx or Rx queue length, the Kernel will double
        // the value provided to allow for sk_buff overhead:
        case S_O_QLEN:

            ;
            int32_t sock_wmem;
            int32_t sock_rmem;

            if (thd_opt->sk_type == SKT_PACKET_MMAP) {
              sock_wmem = (thd_opt->block_sz * thd_opt->block_nr);
              sock_rmem = (thd_opt->block_sz * thd_opt->block_nr);
            } else if (thd_opt->sk_type == SKT_SENDMSG) {
              sock_wmem = (thd_opt->msgvec_vlen * thd_opt->frame_sz);
              sock_rmem = (thd_opt->msgvec_vlen * DEF_FRM_SZ_MAX); ///// align to recvmsg_rx
            } else if (thd_opt->sk_type == SKT_SENDMMSG) {
              sock_wmem = (thd_opt->msgvec_vlen * thd_opt->frame_sz);
              sock_rmem = (thd_opt->msgvec_vlen * DEF_FRM_SZ_MAX); ///// align to recvmsg_rx
            } else {
              return -1; // Unsupported/undefined socket type
            }

            if (thd_opt->sk_mode == SKT_TX) {

                int32_t sock_wmem_cur;
                socklen_t read_len = sizeof(sock_wmem_cur);

                if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur,
                               &read_len) < 0) {

                    perror("Can't get the socket write buffer size");
                    return -1;
                }

                if (sock_wmem_cur < sock_wmem) {

                    if (thd_opt->verbose)
                        printf("Current socket write buffer size is %" PRIi32 " bytes, "
                               "desired write buffer size is %" PRIi32 " bytes.\n"
                               "Trying to increase to %" PRIi32 " bytes...\n",
                               sock_wmem_cur, sock_wmem, sock_wmem);

                    if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem,
                                   sizeof(sock_wmem)) < 0) {

                        perror("Can't set the socket write buffer size");
                        return -1;
                    }
                    
                    if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_wmem_cur,
                                   &read_len) < 0) {

                        perror("Can't get the socket write buffer size");
                        return -1;
                    }


                    printf("Write buffer size set to %" PRIi32 " bytes\n", sock_wmem_cur);


                    if (sock_wmem_cur < sock_wmem) {

                        if (thd_opt->verbose)
                            printf("Write buffer still too small!\n"
                                   "Trying to force to %" PRIi32 " bytes...\n",
                                   sock_wmem);
                        
                        if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUFFORCE,
                                       &sock_wmem, sizeof(sock_wmem)) < 0) {

                            perror("Can't force the socket write buffer size");
                            return -1;
                        }
                        
                        if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF,
                                       &sock_wmem_cur, &read_len) < 0) {

                            perror("Can't get the socket write buffer size");
                            return -1;
                          }
                        
                        // When the buffer size is forced the kernel sets a value double
                        // the requested size to allow for accounting/meta data space
                        if (thd_opt->verbose)
                            printf("Forced write buffer size is now %" PRIi32 " bytes\n", (sock_wmem_cur/2));

                        if (sock_wmem_cur < sock_wmem) {
                            printf("Write buffer still smaller than desired!\n");
                        }

                    }

                }

                return EXIT_SUCCESS;


            // Increase the socket read queue size, the same as above for Tx
            } else if (thd_opt->sk_mode == SKT_RX) {

                int32_t sock_rmem_cur;
                socklen_t read_len = sizeof(sock_rmem_cur);

                if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_RCVBUF, &sock_rmem_cur,
                               &read_len) < 0) {

                    perror("Can't get the socket read buffer size");
                    return -1;
                }

                if (sock_rmem_cur < sock_rmem) {

                    if (thd_opt->verbose)
                        printf("Current socket read buffer size is %" PRIi32 " bytes, "
                               "desired read buffer size is %" PRIi32 " bytes.\n"
                               "Trying to increase to %" PRIi32 " bytes...\n",
                               sock_rmem_cur, sock_rmem, sock_rmem);

                    if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_rmem,
                                   sizeof(sock_rmem)) < 0) {

                        perror("Can't set the socket read buffer size");
                        return -1;
                    }
                    
                    if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF, &sock_rmem_cur,
                                   &read_len) < 0) {

                        perror("Can't get the socket read buffer size");
                        return -1;
                    }


                    printf("Read buffer size set to %" PRIi32 " bytes\n", sock_rmem_cur);


                    if (sock_rmem_cur < sock_rmem) {

                        if (thd_opt->verbose)
                            printf("Read buffer still too small!\n"
                                   "Trying to force to %" PRIi32 " bytes...\n",
                                   sock_rmem);
                        
                        if (setsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUFFORCE,
                                       &sock_rmem, sizeof(sock_rmem))<0) {

                            perror("Can't force the socket read buffer size");
                            return -1;
                        }
                        
                        if (getsockopt(thd_opt->sock_fd, SOL_SOCKET, SO_SNDBUF,
                                       &sock_rmem_cur, &read_len) < 0) {

                            perror("Can't get the socket read buffer size");
                            return -1;
                          }
                        
                        // When the buffer size is forced the kernel sets a value double
                        // the requested size to allow for accounting/meta data space
                        if (thd_opt->verbose)
                            printf("Forced read buffer size is now %" PRIi32 " bytes\n", (sock_rmem_cur/2));

                        if (sock_rmem_cur < sock_rmem) {
                            printf("Read buffer Still smaller than desired!\n");
                        }


                    }

                }

                return EXIT_SUCCESS;

            }


        // Enable Tx ring to skip over malformed packets
        case S_O_LOSSY:

            ;
            static const int32_t sock_discard = 0;
            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_LOSS, (void *)&sock_discard, sizeof(sock_discard));


        // Set the TPACKET version to 2:
        // v2 supports packet level read() and write()
        case S_O_VER_TP2:

            ;
            static const int32_t sock_ver2 = TPACKET_V2;
            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_VERSION, &sock_ver2, sizeof(sock_ver2));

    
        // Set the TPACKET version to 3:
        // v3 supports but block level read() and write()
        case S_O_VER_TP3:

            ;
            static const int32_t sock_ver3 = TPACKET_V3;
            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_VERSION, &sock_ver3, sizeof(sock_ver3));


        // Request hardware timestamping
        case S_O_NIC_TS:

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

            // Set the device/hardware timestamping settings:
            ;
            struct hwtstamp_config hwconfig;
            memset (&hwconfig, 0, sizeof(struct hwtstamp_config));
            hwconfig.tx_type   = HWTSTAMP_TX_OFF;       // Disable all Tx timestamping
            hwconfig.rx_filter = HWTSTAMP_FILTER_NONE;  // Filter all Rx timestamping

            struct ifreq ifr;
            memset (&ifr, 0, sizeof(struct ifreq));
            strncpy (ifr.ifr_name, (char*)thd_opt->if_name, IF_NAMESIZE);
            ifr.ifr_data = (void *) &hwconfig;

            return ioctl(thd_opt->sock_fd, SIOCSHWTSTAMP, &ifr);


        // Set socket timestamping settings
        case S_O_TS:

            ;
            int32_t timesource = 0;
            timesource |= SOF_TIMESTAMPING_RX_HARDWARE;    // Set Rx timestamps to hardware
            timesource |= SOF_TIMESTAMPING_RAW_HARDWARE;   // Use hardware time stamps for reporting
            //timesource |= SOF_TIMESTAMPING_SYS_HARDWARE; // deprecated

            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TIMESTAMP, &timesource, sizeof(timesource));


        // Bypass the kernel qdisc layer and push packets directly to the driver,
        // (packet are not buffered, tc disciplines are ignored, Tx support only).
        // This was added in Linux 3.14.
        case S_O_QDISC:

            ;
            #if !defined(PACKET_QDISC_BYPASS)
                return EXIT_SUCCESS;
            #else
                static const int32_t sock_qdisc_bypass = 1;
                return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));
            #endif
            

        // Define a TPACKET v2 Tx/Rx ring buffer
        case S_O_RING_TP2:

            memset(&thd_opt->tpacket_req,  0, sizeof(struct tpacket_req));
        
            thd_opt->tpacket_req.tp_block_size = thd_opt->block_sz;
            thd_opt->tpacket_req.tp_frame_size = thd_opt->block_frm_sz; // tp_frame_size = TPACKET2_HDRLEN + frame_sz
            thd_opt->tpacket_req.tp_block_nr   = thd_opt->block_nr;
            thd_opt->tpacket_req.tp_frame_nr   = thd_opt->frame_nr;

            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_TX_RING, (void*)&thd_opt->tpacket_req, sizeof(struct tpacket_req));


        // Define a TPACKET v3 Tx/Rx ring buffer
        case S_O_RING_TP3:

            memset(&thd_opt->tpacket_req3, 0, sizeof(struct tpacket_req3));

            thd_opt->tpacket_req3.tp_block_size = thd_opt->block_sz;
            thd_opt->tpacket_req3.tp_frame_size = thd_opt->block_frm_sz;
            thd_opt->tpacket_req3.tp_block_nr   = thd_opt->block_nr;
            thd_opt->tpacket_req3.tp_frame_nr   = thd_opt->frame_nr; ///// (thd_opt->block_sz * thd_opt->block_nr) / thd_opt->block_frm_sz;
            thd_opt->tpacket_req3.tp_retire_blk_tov   = 1; ////// Timeout in msec, what does this do?
            thd_opt->tpacket_req3.tp_feature_req_word = 0; //TP_FT_REQ_FILL_RXHASH;  ///// What does this do?

            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_RX_RING, (void*)&thd_opt->tpacket_req3, sizeof(thd_opt->tpacket_req3));


        // mmap() the Tx/Rx ring buffer against the socket, for TPACKET_V2/3
        case S_O_MMAP_TP23:

            thd_opt->mmap_buf = mmap(NULL, (thd_opt->block_sz * thd_opt->block_nr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED | MAP_POPULATE, thd_opt->sock_fd, 0);

            if (thd_opt->mmap_buf == MAP_FAILED) {
                return -1;
            } else {
                return EXIT_SUCCESS;
            }


        case S_O_FANOUT:

            ;
            uint16_t fanout_type = PACKET_FANOUT_CPU; 
            uint32_t fanout_arg = (thd_opt->fanout_grp | (fanout_type << 16));
            return setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg));


        // Undefined socket operation
        default:
            
            return -1;

    }

}
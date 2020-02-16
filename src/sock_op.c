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



#include "sock_op.h"


int32_t sock_op(uint8_t op, struct thd_opt *thd_opt) {

    switch(op) {


        // Bind socket to interface
        case S_O_BIND:

            memset(&thd_opt->bind_addr, 0, sizeof(thd_opt->bind_addr));
            thd_opt->bind_addr.sll_family   = AF_PACKET;
            thd_opt->bind_addr.sll_protocol = htons(ETH_P_ALL);
            thd_opt->bind_addr.sll_ifindex  = thd_opt->if_index;

            return bind(thd_opt->sock, (struct sockaddr *)&thd_opt->bind_addr,
                        sizeof(thd_opt->bind_addr));


        // Set the socket Tx or Rx queue length, the Kernel will double
        // the value provided to allow for sk_buff overhead:
        case S_O_QLEN:

            ;
            static int32_t sock_wmem;
            static int32_t sock_rmem;

            if (thd_opt->sk_type == SKT_PACKET_MMAP2) {
              sock_wmem = (thd_opt->block_sz * thd_opt->block_nr);
              sock_rmem = (thd_opt->block_sz * thd_opt->block_nr);

            } else if (thd_opt->sk_type == SKT_PACKET_MMAP3) {
              sock_wmem = (thd_opt->block_sz * thd_opt->block_nr);
              sock_rmem = (thd_opt->block_sz * thd_opt->block_nr);

            } else if (thd_opt->sk_type == SKT_SENDMSG) {
              sock_wmem = (thd_opt->msgvec_vlen * thd_opt->frame_sz);
              sock_rmem = (thd_opt->msgvec_vlen * DEF_FRM_SZ_MAX); ///// align to recvmsg_rx

            } else if (thd_opt->sk_type == SKT_SENDMMSG) {
              sock_wmem = (thd_opt->msgvec_vlen * thd_opt->frame_sz);
              sock_rmem = (thd_opt->msgvec_vlen * DEF_FRM_SZ_MAX); ///// align to recvmsg_rx

            } else {
              return -1; // Unsupported/undefined socket type //// CHANGE to DEF_SKT_TYPE???
              
            }

            if (thd_opt->sk_mode == SKT_TX) {

                static int32_t sock_wmem_cur;
                socklen_t read_len = sizeof(sock_wmem_cur);

                if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                               &sock_wmem_cur, &read_len) < 0) {

                    perror("Can't get the socket write buffer size");
                    return -1;
                }

                if (sock_wmem_cur < sock_wmem) {

                    if (thd_opt->verbose)
                        printf("%" PRIu32 ":Current socket write buffer size is %" PRId32
                               " bytes, desired write buffer size is %" PRId32 " bytes.\n",
                               thd_opt->thd_id, sock_wmem_cur, sock_wmem);
                        printf("%" PRIu32 ":Trying to increase to %" PRId32 " bytes...\n",
                               thd_opt->thd_id, sock_wmem);

                    if (setsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF, &sock_wmem,
                                   sizeof(sock_wmem)) < 0) {

                        perror("Can't set the socket write buffer size");
                        return -1;
                    }
                    
                    if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                                   &sock_wmem_cur, &read_len) < 0) {

                        perror("Can't get the socket write buffer size");
                        return -1;
                    }


                    printf("%" PRIu32 ":Write buffer size set to %" PRId32 " bytes\n",
                           thd_opt->thd_id, sock_wmem_cur);


                    if (sock_wmem_cur < sock_wmem) {

                        if (thd_opt->verbose)
                            printf("%" PRIu32 ":Write buffer still too small!"
                                   " Trying to force to %" PRId32 " bytes...\n",
                                   thd_opt->thd_id, sock_wmem);
                        
                        if (setsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUFFORCE,
                                       &sock_wmem, sizeof(sock_wmem)) < 0) {

                            perror("Can't force the socket write buffer size");
                            return -1;
                        }
                        
                        if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                                       &sock_wmem_cur, &read_len) < 0) {

                            perror("Can't get the socket write buffer size");
                            return -1;
                          }
                        
                        // When the buffer size is forced the kernel sets a value double
                        // the requested size to allow for accounting/meta data space
                        if (thd_opt->verbose)
                            printf("%" PRIu32 ":Forced write buffer size is now %" PRId32 " bytes\n",
                                   thd_opt->thd_id, (sock_wmem_cur/2));

                        if (sock_wmem_cur < sock_wmem) {
                            printf("%" PRIu32 ":Write buffer still smaller than desired!\n",
                                   thd_opt->thd_id);
                        }

                    }

                }

                return EXIT_SUCCESS;


            // Increase the socket read queue size, the same as above for Tx
            } else if (thd_opt->sk_mode == SKT_RX) {

                static int32_t sock_rmem_cur;
                socklen_t read_len = sizeof(sock_rmem_cur);

                if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_RCVBUF,
                               &sock_rmem_cur, &read_len) < 0) {

                    perror("Can't get the socket read buffer size");
                    return -1;
                }

                if (sock_rmem_cur < sock_rmem) {

                    if (thd_opt->verbose)
                        printf("%" PRIu32 ":Current socket read buffer size is %" PRId32
                               " bytes, desired read buffer size is %" PRId32 " bytes.\n",
                               thd_opt->thd_id, sock_rmem_cur, sock_rmem);
                        printf("%" PRIu32 ":Trying to increase to %" PRId32 " bytes...\n",
                               thd_opt->thd_id, sock_rmem);

                    if (setsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                                   &sock_rmem, sizeof(sock_rmem)) < 0) {

                        perror("Can't set the socket read buffer size");
                        return -1;
                    }
                    
                    if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                                   &sock_rmem_cur, &read_len) < 0) {

                        perror("Can't get the socket read buffer size");
                        return -1;
                    }


                    printf("%" PRIu32 ":Read buffer size set to %" PRId32 " bytes\n",
                           thd_opt->thd_id, sock_rmem_cur);


                    if (sock_rmem_cur < sock_rmem) {

                        if (thd_opt->verbose)
                            printf("%" PRIu32 ":Read buffer still too small!"
                                   " Trying to force to %" PRId32 " bytes...\n",
                                   thd_opt->thd_id, sock_rmem);
                        
                        if (setsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUFFORCE,
                                       &sock_rmem, sizeof(sock_rmem))<0) {

                            perror("Can't force the socket read buffer size");
                            return -1;
                        }
                        
                        if (getsockopt(thd_opt->sock, SOL_SOCKET, SO_SNDBUF,
                                       &sock_rmem_cur, &read_len) < 0) {

                            perror("Can't get the socket read buffer size");
                            return -1;
                          }
                        
                        // When the buffer size is forced the kernel sets a value double
                        // the requested size to allow for accounting/meta data space
                        if (thd_opt->verbose)
                            printf("%" PRIu32 ":Forced read buffer size is now %" PRId32 " bytes\n",
                                   thd_opt->thd_id, (sock_rmem_cur/2));

                        if (sock_rmem_cur < sock_rmem) {
                            printf("%" PRIu32 ":Read buffer Still smaller than desired!\n",
                                   thd_opt->thd_id);
                        }


                    }

                }

                return EXIT_SUCCESS;

            }


        // Enable Tx ring to skip over malformed packets
        case S_O_LOSSY:

            ;
            #if !defined(PACKET_LOSS) // Requires Kernel 2.6.31
            return EXIT_SUCCESS;
            #else
            static const int32_t sock_discard = 1;
            return setsockopt(thd_opt->sock, SOL_PACKET, PACKET_LOSS, &sock_discard, sizeof(sock_discard));
            #endif


        // Set the TPACKET version:
        // v2 supports packet level read() and write()
        // v3 supports but block level read() and write()
        case S_O_VER_TP:

            ;
            return setsockopt(thd_opt->sock, SOL_PACKET, PACKET_VERSION, &thd_opt->tpacket_ver, sizeof(thd_opt->tpacket_ver));
            

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
            static struct hwtstamp_config hwconfig;
            memset (&hwconfig, 0, sizeof(struct hwtstamp_config));
            hwconfig.tx_type   = HWTSTAMP_TX_OFF;       // Disable all Tx timestamping
            hwconfig.rx_filter = HWTSTAMP_FILTER_NONE;  // Filter all Rx timestamping

            static struct ifreq ifr;
            memset (&ifr, 0, sizeof(struct ifreq));
            strncpy (ifr.ifr_name, (char*)thd_opt->if_name, IF_NAMESIZE);
            ifr.ifr_data = (void *) &hwconfig;

            return ioctl(thd_opt->sock, SIOCSHWTSTAMP, &ifr);


        // Set socket timestamping settings
        case S_O_TS:

            ;
            static int32_t timesource = 0;
            timesource |= SOF_TIMESTAMPING_RX_HARDWARE;    // Set Rx timestamps to hardware
            timesource |= SOF_TIMESTAMPING_RAW_HARDWARE;   // Use hardware time stamps for reporting
            //timesource |= SOF_TIMESTAMPING_SYS_HARDWARE; // deprecated

            return setsockopt(thd_opt->sock, SOL_PACKET, PACKET_TIMESTAMP, &timesource, sizeof(timesource));


        // Bypass the kernel qdisc layer and push packets directly to the driver,
        // (packet are not buffered, tc disciplines are ignored, Tx support only).
        // This was added in Linux 3.14.
        case S_O_QDISC:

            ;
            #if !defined(PACKET_QDISC_BYPASS)
                return EXIT_SUCCESS;
            #else
                static const int32_t sock_qdisc_bypass = 1;
                return setsockopt(thd_opt->sock, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass));
            #endif
            

        // Define a TPACKET v2 Tx/Rx ring buffer
        case S_O_RING_TP2:

            return setsockopt(thd_opt->sock, SOL_PACKET, thd_opt->ring_type, thd_opt->tpacket_req, thd_opt->tpacket_req_sz);


        // Define a TPACKET v3 Tx/Rx ring buffer
        case S_O_RING_TP3:

            return setsockopt(thd_opt->sock, SOL_PACKET, thd_opt->ring_type, thd_opt->tpacket_req3, thd_opt->tpacket_req3_sz);


        // mmap() the Tx/Rx ring buffer against the socket, for TPACKET_V2/3
        case S_O_MMAP_TP23:

            thd_opt->mmap_buf = mmap(NULL, (thd_opt->block_sz * thd_opt->block_nr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED | MAP_POPULATE, thd_opt->sock, 0);

            if (thd_opt->mmap_buf == MAP_FAILED) {
                return -1;
            } else {
                return EXIT_SUCCESS;
            }


        case S_O_FANOUT:

            ;
            static const uint16_t fanout_type = PACKET_FANOUT_CPU;
            static uint32_t fanout_arg;
            fanout_arg = (thd_opt->fanout_grp | (fanout_type << 16));
            return setsockopt(thd_opt->sock, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg));


        // Undefined socket operation
        default:
            
            return -1;

    }

}
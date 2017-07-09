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



#include "packet.h"



void *packet_rx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = packet_setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    int16_t rx_bytes;
    /////thd_opt->started = 1;

    while(1) {

        rx_bytes = read(thd_opt->sock_fd, thd_opt->rx_buffer, DEF_FRM_SZ_MAX);
        
        if (rx_bytes == -1) {
            perror("Socket Rx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->rx_bytes += rx_bytes;
        thd_opt->rx_pkts += 1;

    }

}



int32_t packet_setup_socket(struct thd_opt *thd_opt) {

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



    // Enable packet loss, only supported on Tx
    ///// Does this have any effect on non-Tx ring transmit?
    if (thd_opt->sk_mode == SKT_TX) {
        
        static const int32_t sock_discard = 0;
        int32_t sock_loss_ret = setsockopt(thd_opt->sock_fd, SOL_PACKET, PACKET_LOSS, (void *)&sock_discard, sizeof(sock_discard));

        if (sock_loss_ret == -1) {
            perror("Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
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



void *packet_tx(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = packet_setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    int16_t tx_bytes;
    /////thd_opt->started = 1;

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

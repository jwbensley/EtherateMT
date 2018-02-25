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



#include "packet_msg.h"



void *rx_recvmsg(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = msg_setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }


    int32_t rx_bytes = 0;
    /////thd_opt->started = 1;

    struct msghdr msg_hdr;
    struct iovec iov;
    memset(&msg_hdr, 0, sizeof(msg_hdr));
    memset(&iov, 0, sizeof(iov));

    iov.iov_base = thd_opt->rx_buffer;
    iov.iov_len = thd_opt->frame_sz;

    msg_hdr.msg_name = NULL;
    msg_hdr.msg_iov = &iov;
    msg_hdr.msg_iovlen = 1;
    msg_hdr.msg_control = NULL;
    msg_hdr.msg_controllen = 0;

    while(1) {

        rx_bytes = recvmsg(thd_opt->sock_fd, &msg_hdr, 0);
        
        if (rx_bytes == -1) {
            perror("Socket Rx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->rx_bytes += rx_bytes;
        thd_opt->rx_pkts += 1;

    }

}



int32_t msg_setup_socket(struct thd_opt *thd_opt) {

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
    int32_t sock_bind = sock_op(S_O_BIND, thd_opt);

    if (sock_bind == -1) {
        perror("Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bypass the kernel qdisc layer and push packets directly to the driver
    int32_t sock_qdisc = sock_op(S_O_QDISC, thd_opt);

    if (sock_qdisc == -1) {
        perror("Can't enable QDISC bypass on socket");
        return EXIT_FAILURE;
    }


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
    }


    // Join this socket to the fanout group
    if (thd_opt->thd_cnt > 1) {

        //if (thd_opt->verbose)
        //    printf("Joining thread %" PRIu16 "" to fanout group %" PRId32 "...\n", thd_opt->thd_id, thd_opt->fanout_grp); ///// Add thread ID/number

        int32_t sock_fanout = sock_op(S_O_FANOUT, thd_opt);

        if (sock_fanout < 0) {
            perror("Can't configure fanout");
            return EXIT_FAILURE;
        }

    }


    return EXIT_SUCCESS;

}



void *tx_sendmsg(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    int32_t sk_setup_ret = msg_setup_socket(thd_opt_p);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    int32_t tx_bytes;

    struct msghdr msg_hdr;
    struct iovec iov;
    memset(&msg_hdr, 0, sizeof(msg_hdr));
    memset(&iov, 0, sizeof(iov));

    iov.iov_base = thd_opt->tx_buffer;
    iov.iov_len = thd_opt->frame_sz;

    msg_hdr.msg_iov = &iov;
    msg_hdr.msg_iovlen = 1;

    while (1) {

        tx_bytes = sendmsg(thd_opt->sock_fd, &msg_hdr, 0); //// Is MSG_DONTWAIT supported? Would it make any difference?

        if (tx_bytes == -1) {
            printf("Socket Tx error (%d): %s\n", errno, strerror(errno));
            //perror("Socket Tx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->tx_bytes += tx_bytes;
        thd_opt->tx_pkts += 1;
    }

}
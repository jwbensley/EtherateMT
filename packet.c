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



#include "packet.h"



void *packet_init(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;


    pid_t thread_id;
    thread_id = syscall(SYS_gettid);
    thd_opt->thd_id = thread_id;
    
    if (thd_opt->verbose)
        printf("Worker thread %" PRIu32 " started\n", thread_id);


    int32_t sk_setup_ret = packet_sock(thd_opt);
    if (sk_setup_ret != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }


    packet_sock(thd_opt_p);

    if (thd_opt->sk_mode == SKT_RX) {
        packet_rx(thd_opt_p);
    } else if (thd_opt->sk_mode == SKT_TX) {
        packet_tx(thd_opt_p);
    }

    return NULL; ///// Need to check this

}



int32_t packet_sock(struct thd_opt *thd_opt) {

    // Create a raw socket
    thd_opt->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock_fd == -1) {
        tperror(thd_opt, "Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Enable promiscuous mode
    int32_t sock_promisc = sock_op(S_O_PROMISC_ADD, thd_opt);

    if (sock_promisc == -1) {
        tperror(thd_opt, "Can't enable promisc mode");
        return EXIT_FAILURE;
    }


    // Bind socket to interface.
    int32_t sock_bind = sock_op(S_O_BIND, thd_opt);

    if (sock_bind == -1) {
        tperror(thd_opt, "Can't bind to AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bypass the kernel qdisc layer and push packets directly to the driver
    int32_t sock_qdisc = sock_op(S_O_QDISC, thd_opt);

    if (sock_qdisc == -1) {
        tperror(thd_opt, "Can't enable QDISC bypass on socket");
        return EXIT_FAILURE;
    }


    // Enable Tx ring to skip over malformed packets
    if (thd_opt->sk_mode == SKT_TX) {

        int32_t sock_lossy = sock_op(S_O_LOSSY, thd_opt);

        if (sock_lossy == -1) {
            tperror(thd_opt, "Can't enable PACKET_LOSS on socket");
            return EXIT_FAILURE;
        }
        
    }


    // Request hardware timestamping of received packets
    int32_t sock_nic_ts = sock_op(S_O_NIC_TS, thd_opt);

    if (sock_nic_ts == -1) {
        tperror(thd_opt, "Couldn't set ring timestamp source");
        // If hardware timestamps aren't supported the Kernel will fall back to
        // software, no need to exit on error
    }


    // Set the socket timestamping settings:
    int32_t sock_ts = sock_op(S_O_TS, thd_opt);

    if (sock_ts == -1) {
        tperror(thd_opt, "Couldn't set socket Rx timestamp source");
    }


    // Join this socket to the fanout group
    if (thd_opt->thd_nr > 1) {

        int32_t sock_fanout = sock_op(S_O_FANOUT, thd_opt);

        if (sock_fanout < 0) {
            tperror(thd_opt, "Can't configure fanout");
            return EXIT_FAILURE;
        } else {
            if (thd_opt->verbose)
                printf("%" PRIu32 ":Joint fanout group %" PRId32 "\n",
                       thd_opt->thd_id, thd_opt->fanout_grp);
        }

    }


    return EXIT_SUCCESS;
}



void packet_rx(struct thd_opt *thd_opt) {

    int32_t rx_bytes;
    thd_opt->started = 1;

    while(1) {

        rx_bytes = read(thd_opt->sock_fd, thd_opt->rx_buffer, DEF_FRM_SZ_MAX);
        
        if (rx_bytes == -1) {
            tperror(thd_opt, "Socket Rx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->rx_bytes += rx_bytes;
        thd_opt->rx_pkts += 1;

    }

}



void packet_tx(struct thd_opt *thd_opt) {


    int32_t tx_bytes;
    thd_opt->started = 1;

    while(1) {

        /*
        tx_bytes = sendto(thd_opt->sock_fd, thd_opt->tx_buffer,
                          thd_opt->frame_sz, 0,
                          (struct sockaddr*)&thd_opt->bind_addr,
                          sizeof(thd_opt->bind_addr));
        */
        
        // This is a just a little faster than sendto():
        tx_bytes = send(thd_opt->sock_fd, thd_opt->tx_buffer,
                        thd_opt->frame_sz, 0);        

        if (tx_bytes == -1) {
            tperror(thd_opt, "Socket Tx error");
            exit(EXIT_FAILURE);
        }

        thd_opt->tx_bytes += tx_bytes;
        thd_opt->tx_pkts += 1;

    }

}

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



#include "packet_mmsg.h"



void *mmsg_init(void* thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;


    // Save the thread tid
    pid_t thread_id;
    thread_id = syscall(SYS_gettid);
    thd_opt->thd_id = thread_id;


    // Set the thread cancel type and register the cleanup handler
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push(thd_cleanup, thd_opt_p);
    

    if (thd_opt->verbose) {
        if (thd_opt->affinity >= 0) {
            printf(
                "Worker thread %" PRIu32 " started, bound to CPU %" PRId32 "\n",
                thd_opt->thd_id, thd_opt->affinity
            );
        } else {
            printf("Worker thread %" PRIu32 " started\n", thd_opt->thd_id);
        }
    }

    if (mmsg_sock(thd_opt) != EXIT_SUCCESS) {
        pthread_exit((void*)EXIT_FAILURE);
    }

    if (thd_opt->sk_mode == SKT_RX) {
        mmsg_rx(thd_opt_p);
    } else if (thd_opt->sk_mode == SKT_TX) {
        mmsg_tx(thd_opt_p);
    }


    pthread_cleanup_pop(0);
    return NULL;

}



void mmsg_rx(struct thd_opt *thd_opt) {

    int32_t rx_frames = 0;

    struct mmsghdr mmsg_hdr[thd_opt->msgvec_vlen];
    struct iovec iov[thd_opt->msgvec_vlen];
    memset(mmsg_hdr, 0, sizeof(mmsg_hdr));
    memset(iov, 0, sizeof(iov));

    thd_opt->started = 1;

    for (uint32_t i = 0; i < thd_opt->msgvec_vlen; i += 1) {
        iov[i].iov_base = thd_opt->rx_buffer;
        iov[i].iov_len = thd_opt->frame_sz;
        mmsg_hdr[i].msg_hdr.msg_iov = &iov[i];
        mmsg_hdr[i].msg_hdr.msg_iovlen = 1;
        mmsg_hdr[i].msg_hdr.msg_name = NULL;
        mmsg_hdr[i].msg_hdr.msg_control = NULL;
        mmsg_hdr[i].msg_hdr.msg_controllen = 0;
    }

    while(1) {

        /*
         recvmmsg() returns the number of frames sent or -1 on error.
         If some frames have been received in the vector and then an
         error occurs, recvmmsg returns -1. For the received frames
         msg_len is updated.
        */
        rx_frames = recvmmsg(
            thd_opt->sock, mmsg_hdr, thd_opt->msgvec_vlen, 0, NULL
        );
        
        if (rx_frames == -1) {
            thd_opt->sk_err += 1;
        }

        for (uint32_t i = 0; i < thd_opt->msgvec_vlen; i+= 1) {
            if (mmsg_hdr[i].msg_len > 0) {
                thd_opt->rx_bytes += mmsg_hdr[i].msg_len;
                thd_opt->rx_frms += 1;
            }
        }

    }

}



int32_t mmsg_sock(struct thd_opt *thd_opt) {


    // Create a raw socket
    thd_opt->sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
   
    if (thd_opt->sock == -1) {
        tperror(thd_opt, "Can't create AF_PACKET socket");
        return EXIT_FAILURE;
    }


    // Bind socket to interface
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


    // Increase the socket Tx queue size so that the entire msg vector can fit
    // into the socket Tx/Rx queue. The Kernel will double the value provided
    // to allow for sk_buff overhead:
    if (sock_op(S_O_QLEN, thd_opt) == -1) {
        tperror(thd_opt, "Can't change the socket Tx queue length");
        return EXIT_FAILURE;
    }



    return EXIT_SUCCESS;

}



void mmsg_tx(struct thd_opt *thd_opt) {

    int32_t tx_frames = 0;

    struct mmsghdr mmsg_hdr[thd_opt->msgvec_vlen];
    struct iovec iov[thd_opt->msgvec_vlen];
    memset(mmsg_hdr, 0, sizeof(mmsg_hdr));
    memset(iov, 0, sizeof(iov));

    thd_opt->started = 1;

    for (uint32_t i = 0; i < thd_opt->msgvec_vlen; i += 1) {
        iov[i].iov_base = thd_opt->tx_buffer;
        iov[i].iov_len = thd_opt->frame_sz;
        mmsg_hdr[i].msg_hdr.msg_iov = &iov[i];
        mmsg_hdr[i].msg_hdr.msg_iovlen = 1;
    }


    while (1) {

        /*
         When using sendmmsg() to send a batch of frames, unlike PACKET_MMAP,
         an error is only returned if no datagrams were sent, rather than the
         PACKET_MMAP approach to return an error if any one frame failed to
         send.

         sendmmsg() returns the number of frames sent or -1 and sets errno if
         0 frames were sent.
        */

        tx_frames = sendmmsg(thd_opt->sock, mmsg_hdr, thd_opt->msgvec_vlen, 0);

        if (tx_frames == -1) {
            thd_opt->sk_err += 1;
        } else {
            ///// TODO
            ///// No need to check frame size if all frames are the same size?
            for (uint32_t i = 0; i < thd_opt->msgvec_vlen; i+= 1) {
                thd_opt->tx_bytes += mmsg_hdr[i].msg_len;
            }
            thd_opt->tx_frms += tx_frames;
        }

    }

}
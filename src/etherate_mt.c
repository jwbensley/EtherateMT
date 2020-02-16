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



#include "etherate_mt.h"
#include "threads.h"

#include "functions.c"
#include "sock_op.c"

#include "packet.c"
#include "packet_msg.c"
#include "packet_mmsg.c"

// Only inlcude these files if the required Kernel version is detected,
// otherwise the code won't compile:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
#include "tpacket_v2.c"
#else
#include "tpacket_v2_bypass.c"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
#include "tpacket_v3.c"
#else
#include "tpacket_v3_bypass.c"
#endif

#include "print_stats.c"
#include "threads.c"




int main(int argc, char *argv[]) {

    // Check for root privileges
    if (getuid() != 0) {
        printf("Oops! Must be root to use this program.\n");
        return EX_NOPERM;
    }

    // Global instance of all settings/values
    struct etherate eth;

    // Global pointer to eth object used by signal_handler()
    eth_p = &eth;

    // Declare sigint handler to cancel the worker and stats threads
    signal (SIGINT, signal_handler);

    // Set application defaults    
    etherate_setup(&eth);

    // Process CLI args
    uint16_t cli_ret = cli_args(argc, argv, &eth);


    if (cli_ret == EXIT_FAILURE) {
        etherate_cleanup(&eth);
        return cli_ret;
    } else if (cli_ret == EX_SOFTWARE) {
        etherate_cleanup(&eth);
        return EXIT_SUCCESS;
    }

    // Ensure an interface has been chosen
    if (eth.sk_opt.if_index == -1) {
        printf("Oops! No interface chosen.\n");
        return EX_SOFTWARE;
    }

    if (eth.app_opt.verbose) printf("Verbose output enabled.\n");


    // Put the chosen interface into promisc mode
    int32_t promisc_ret = set_int_promisc(&eth);
    if (promisc_ret != EXIT_SUCCESS)
        return promisc_ret;


    printf("Frame size set to %" PRIu16 " bytes.\n", eth.frm_opt.frame_sz);


    if (eth.app_opt.sk_type == SKT_PACKET_MMAP2) {
        printf("Using raw socket with PACKET_MMAP and TX/RX_RING v2.\n");
    } else if (eth.app_opt.sk_type == SKT_PACKET) {
        printf("Using raw packet socket with send()/read().\n");
    } else if (eth.app_opt.sk_type == SKT_SENDMSG) {
        printf("Using raw packet socket with sendmsg()/recvmsg().\n");
    } else if (eth.app_opt.sk_type == SKT_SENDMMSG) {
        printf("Using raw packet socket with sendmmsg()/recvmmsg().\n");
    } else if (eth.app_opt.sk_type == SKT_PACKET_MMAP3) {
        printf("Using raw socket with PACKET_MMAP and TX/RX_RING v3.\n");
    }

    if (eth.app_opt.sk_mode == SKT_RX) {
        printf("Running in Rx mode.\n");
    } else if (eth.app_opt.sk_mode == SKT_TX) {
        printf("Running in Tx mode.\n");
    } else if (eth.app_opt.sk_mode == SKT_BIDI) {
        printf("Running in bidirectional mode.\n");
    }

    
    if (eth.app_opt.verbose)
        printf("Main thread pid is %" PRId32 ".\n", getpid());

    
    // Fill the test frame buffer with random data
    if (getrandom(eth.frm_opt.tx_buffer, eth.frm_opt.frame_sz, 0)
        != eth.frm_opt.frame_sz)
    {
        perror("Can't generate random frame data");
        exit(EXIT_FAILURE);
    }


    thd_alloc(&eth);
    // Spawn the stats printing thread
    if (thd_init_stats(&eth) != EXIT_SUCCESS)
        return EXIT_FAILURE;


    // Create a copy of the program settings for each worker thread
    eth.thd_opt = calloc(sizeof(struct thd_opt), eth.app_opt.thd_nr);

    // Spawn each worker thread
    for (uint16_t thread = 0; thread < eth.app_opt.thd_nr; thread += 1) {

        pthread_attr_init(&eth.app_opt.thd_attr[thread]);
        pthread_attr_setdetachstate(&eth.app_opt.thd_attr[thread], PTHREAD_CREATE_JOINABLE);

        // Setup and copy default per-thread structures and settings
        thd_setup(&eth, thread);

        if (thd_init_worker(&eth, thread) != EXIT_SUCCESS)
            return EXIT_FAILURE;

    }

    // Wait for worker and stats threads to finish, then clean up
    thd_join_workers(&eth);
    thd_join_stats(&eth);
    etherate_cleanup(&eth);

}
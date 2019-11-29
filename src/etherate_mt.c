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



#include "etherate_mt.h"
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
    uint16_t cli_args_ret = cli_args(argc, argv, &eth);

    if (cli_args_ret == EXIT_FAILURE) {
        etherate_cleanup(&eth);
        return cli_args_ret;
    } else if (cli_args_ret == EX_SOFTWARE) {
        etherate_cleanup(&eth);
        return EXIT_SUCCESS;
    }


    if (eth.app_opt.verbose) printf("Verbose output enabled.\n");


    // Ensure an interface has been chosen
    if (eth.sk_opt.if_index == -1) {
        printf("Oops! No interface chosen.\n");
        return EX_SOFTWARE;
    }


    // Put the interface into promisc mode
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
    if (eth.frm_opt.custom_frame == 0) {
        for (uint16_t i = 0; i < eth.frm_opt.frame_sz; i += 1)
        {
            eth.frm_opt.tx_buffer[i] = (uint8_t)((255.0*rand()/(RAND_MAX+1.0)));
        }
    }




    // Spawn the stats printing thread
    thd_init(&eth);
    if (spawn_stats_thd(&eth) != EXIT_SUCCESS)
        return EXIT_FAILURE;


    // Create a copy of the program settings for each worker thread
    eth.thd_opt = calloc(sizeof(struct thd_opt), eth.app_opt.thd_nr);

    for (uint16_t thread = 0; thread < eth.app_opt.thd_nr; thread += 1) {

        pthread_attr_init(&eth.app_opt.thd_attr[thread]);
        pthread_attr_setdetachstate(&eth.app_opt.thd_attr[thread], PTHREAD_CREATE_JOINABLE);

        ///// Set thread priorities?
        /*
        pthread_attr_init(&t_attr_send);
        pthread_attr_init(&t_attr_fill);
     
        pthread_attr_setschedpolicy(&t_attr_send,SCHED_RR);
        pthread_attr_setschedpolicy(&t_attr_fill,SCHED_RR);
     
        para_send.sched_priority=20;
        pthread_attr_setschedparam(&t_attr_send,&para_send);
        para_fill.sched_priority=20;
        pthread_attr_setschedparam(&t_attr_fill,&para_fill);
        */

        // Setup and copy default per-thread settings
        thread_init(&eth, thread);

        ///// Perhaps get the thread to set its own affinity first before it does anything else?
        if (eth.app_opt.thd_affin) {
            cpu_set_t current_cpu_set;

            int cpu_to_bind = thread % eth.app_opt.thd_nr;
            CPU_ZERO(&current_cpu_set);
            // We count cpus from zero
            CPU_SET(cpu_to_bind, &current_cpu_set);

            /////int set_affinity_result = pthread_attr_setaffinity_np(thread_attrs.native_handle(), sizeof(cpu_set_t), &current_cpu_set);
            int set_affinity_result = pthread_attr_setaffinity_np(&eth.app_opt.thd_attr[thread], sizeof(cpu_set_t), &current_cpu_set);

            if (set_affinity_result != 0) {
                printf("Can't set CPU affinity for thread\n");
            }

            /*

            int ret = setpriority (PRIO_PROCESS, getpid (), priority);
            if (ret)
                panic ("Can't set nice val\n");
            }
            */

        }

        int32_t thd_ret = 0;

        if (eth.app_opt.sk_type == SKT_PACKET_MMAP2) {
            thd_ret = pthread_create(&eth.app_opt.thd[thread], &eth.app_opt.thd_attr[thread], tpacket_v2_init, (void*)&eth.thd_opt[thread]);

        } else if (eth.app_opt.sk_type == SKT_PACKET) {
            thd_ret = pthread_create(&eth.app_opt.thd[thread], &eth.app_opt.thd_attr[thread], packet_init, (void*)&eth.thd_opt[thread]);

        } else if (eth.app_opt.sk_type == SKT_SENDMSG) {
            thd_ret = pthread_create(&eth.app_opt.thd[thread], &eth.app_opt.thd_attr[thread], msg_init, (void*)&eth.thd_opt[thread]);

        } else if (eth.app_opt.sk_type == SKT_SENDMMSG) {
            thd_ret = pthread_create(&eth.app_opt.thd[thread], &eth.app_opt.thd_attr[thread], mmsg_init, (void*)&eth.thd_opt[thread]);

        } else if (eth.app_opt.sk_type == SKT_PACKET_MMAP3) {
            thd_ret = pthread_create(&eth.app_opt.thd[thread], &eth.app_opt.thd_attr[thread], tpacket_v3_init, (void*)&eth.thd_opt[thread]);

        }

        if (thd_ret != 0) {
            perror("Can't create worker thread");
            exit(EXIT_FAILURE);
        }

        if (pthread_attr_destroy(&eth.app_opt.thd_attr[thread]) != 0) {
            perror("Can't remove worker thread attributes");
        }

    }


    // Free attributes and wait for the worker threads to finish
    for(uint16_t thread = 0; thread < eth.app_opt.thd_nr; thread += 1) {
        
        if (pthread_attr_destroy(&eth.app_opt.thd_attr[thread]) != 0) {
            perror("Can't remove thread attributes");
        }
        
        int32_t thd_ret;
        int32_t join_ret = pthread_join(eth.app_opt.thd[thread], (void*)&thd_ret);

        if (join_ret != 0)
            printf("Can't join worker thread %" PRIu32 ", return code is %" PRId32 "\n", eth.thd_opt[thread].thd_id, join_ret);

        if (thd_ret != EXIT_SUCCESS) {
            if (eth.app_opt.verbose)
                printf("Worker thread %" PRIu32 " returned %" PRId32 "\n", eth.thd_opt[thread].thd_id, thd_ret);
            eth.thd_opt[thread].quit = 1;
        }

        thread_cleanup(&eth.thd_opt[thread]);

    }


    // Free attributes and wait for the stats thread to finish
    if (pthread_attr_destroy(&eth.app_opt.thd_attr[eth.app_opt.thd_nr]) != 0) {
        perror("Can't remove thread attributes");
    }

    int32_t thd_ret;
    int32_t join_ret = pthread_join(eth.app_opt.thd[eth.app_opt.thd_nr], (void*)&thd_ret);

    if (join_ret != 0)
        printf("Can't join stats thread, return code is %" PRId32 "\n", join_ret);

    if (thd_ret != EXIT_SUCCESS) {
        if (eth.app_opt.verbose)
            printf("Completed join with stats thread with a status of %" PRId32 "\n", thd_ret);
    }


    etherate_cleanup(&eth);

}
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



int main(int argc, char *argv[]) {

    // Global instance of all settings/values
    struct etherate etherate;

    // Create options and set application defaults    
    etherate_setup(&etherate);

    // Process CLI args
    uint16_t cli_args_ret = cli_args(argc, argv, &etherate);

    if (cli_args_ret == EXIT_FAILURE) {
        free(etherate.frm_opt.tx_buffer);
        return cli_args_ret;

    } else if (cli_args_ret == EX_SOFTWARE) {
        free(etherate.frm_opt.tx_buffer);
        return EXIT_SUCCESS;
    }


    // Check for root privileges
    if (getuid() != 0) {
        printf("Oops! Must be root to use this program.\n");
        return EX_NOPERM;
    }

    if (etherate.app_opt.verbose) printf("Verbose output enabled.\n");

    // Ensure an interface has been chosen
    if (etherate.sk_opt.if_index == -1) {
        printf("Oops! No interface chosen.\n");
        return EX_SOFTWARE;
    }

    printf("Frame size set to %" PRIu16 " bytes.\n", etherate.frm_opt.frame_sz);

    if (etherate.app_opt.sk_type == SKT_PACKET_MMAP2) {
        printf("Using raw socket with PACKET_MMAP and TX/RX_RING v2.\n");
    } else if (etherate.app_opt.sk_type == SKT_PACKET) {
        printf("Using raw packet socket with send()/read().\n");
    } else if (etherate.app_opt.sk_type == SKT_SENDMSG) {
        printf("Using raw packet socket with sendmsg()/recvmsg().\n");
    } else if (etherate.app_opt.sk_type == SKT_SENDMMSG) {
        printf("Using raw packet socket with sendmmsg()/recvmmsg().\n");
    } else if (etherate.app_opt.sk_type == SKT_PACKET_MMAP3) {
        printf("Using raw socket with PACKET_MMAP and TX/RX_RING v3.\n");
    }

    if (etherate.app_opt.sk_mode == SKT_RX) {
        printf("Running in Rx mode.\n");
    } else if (etherate.app_opt.sk_mode == SKT_TX) {
        printf("Running in Tx mode.\n");
    } else if (etherate.app_opt.sk_mode == SKT_BIDI) { ///// Currently does nothing
        printf("Running in bidirectional mode.\n");
    }

    
    if (etherate.app_opt.verbose)
        printf("Main thread pid is %" PRIu32 ".\n", getpid());

    
    // Fill the test frame buffer with random data
    if (etherate.frm_opt.custom_frame == 0) {
        for (uint16_t i = 0; i < etherate.frm_opt.frame_sz; i += 1)
        {
            etherate.frm_opt.tx_buffer[i] = (uint8_t)((255.0*rand()/(RAND_MAX+1.0)));
        }
    }


    pthread_t worker_thread[etherate.app_opt.thd_nr];
    pthread_attr_t worker_attr[etherate.app_opt.thd_nr];


    // Create a copy of the program settings for each worker thread
    etherate.thd_opt = calloc(sizeof(struct thd_opt), etherate.app_opt.thd_nr);

    for (uint16_t thread = 0; thread < etherate.app_opt.thd_nr; thread += 1) {

        pthread_attr_init(&worker_attr[thread]);
        pthread_attr_setdetachstate(&worker_attr[thread], PTHREAD_CREATE_JOINABLE);


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
        thread_init(&etherate, thread);

        ///// Perhaps get the thread to set its own affinity first before it does anything else?
        if (etherate.app_opt.thd_sk_affin) {
            cpu_set_t current_cpu_set;

            int cpu_to_bind = thread % etherate.app_opt.thd_nr;
            CPU_ZERO(&current_cpu_set);
            // We count cpus from zero
            CPU_SET(cpu_to_bind, &current_cpu_set);

            /////int set_affinity_result = pthread_attr_setaffinity_np(thread_attrs.native_handle(), sizeof(cpu_set_t), &current_cpu_set);
            int set_affinity_result = pthread_attr_setaffinity_np(&worker_attr[thread], sizeof(cpu_set_t), &current_cpu_set);

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

        uint32_t worker_thread_ret = 0;

        if (etherate.app_opt.sk_type == SKT_PACKET_MMAP2) {
            worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tpacket_v2_init, (void*)&etherate.thd_opt[thread]);
        } else if (etherate.app_opt.sk_type == SKT_PACKET) {
            worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], packet_init, (void*)&etherate.thd_opt[thread]);
        } else if (etherate.app_opt.sk_type == SKT_SENDMSG) {
            worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], msg_init, (void*)&etherate.thd_opt[thread]);
        } else if (etherate.app_opt.sk_type == SKT_SENDMMSG) {
            worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], mmsg_init, (void*)&etherate.thd_opt[thread]);
        } else if (etherate.app_opt.sk_type == SKT_PACKET_MMAP3) {
            worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tpacket_v3_init, (void*)&etherate.thd_opt[thread]);
        }

        if (worker_thread_ret) {
            printf("Return code from worker thread creation is %" PRIi32 "\n", worker_thread_ret);
            exit(EXIT_FAILURE);
        }/* else {
            if (etherate.app_opt.verbose)
                printf("Started worker thread %" PRIu64 "\n", worker_thread[thread]);
        }*/

    }


    // Spawn a stats printing thread
    pthread_t stats_thread;
    int32_t stats_thread_ret = pthread_create(&stats_thread, NULL, print_stats, (void*)&etherate);


    // Free attribute and wait for the worker threads to finish
    for(uint16_t thread = 0; thread < etherate.app_opt.thd_nr; thread += 1) {
        
        pthread_attr_destroy(&worker_attr[thread]);
        void *thread_status;
        int32_t worker_join_ret = pthread_join(worker_thread[thread], &thread_status);
        
        munmap(etherate.thd_opt[thread].mmap_buf, (etherate.thd_opt[thread].block_sz * etherate.thd_opt[thread].block_nr));
        close(etherate.thd_opt[thread].sock_fd);
        free(etherate.thd_opt[thread].rx_buffer);
        free(etherate.thd_opt[thread].tx_buffer);

        if (worker_join_ret) {
            printf("Return code from worker thread join is %" PRIi32 "\n", worker_join_ret);
            exit(EXIT_FAILURE);
        }
        printf("Main: completed join with thread %" PRIi32 " having a status of %" PRIi64 "\n", thread, (long)thread_status);
    }

    free(etherate.thd_opt);
    free(etherate.frm_opt.tx_buffer);

    void *thread_status;
    stats_thread_ret = pthread_join(stats_thread, &thread_status);
    if (stats_thread_ret) {
        printf("Return code from stats thread join is %" PRIi32 "\n", stats_thread_ret);
        exit(EXIT_FAILURE);
    }


}
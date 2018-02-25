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
#include "tpacket_v2.c"
#include "tpacket_v3.c"



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
        printf("Must be root to use this program!\n");
        return EX_NOPERM;
    }

    if (etherate.app_opt.verbose) printf("Verbose output enabled.\n");

    // Ensure an interface has been chosen
    if (etherate.sk_opt.if_index == 0) {
        printf("No interface chosen!\n");
        return EX_SOFTWARE;
    }

    printf("Frame size set to %" PRIu16 " bytes\n", etherate.frm_opt.frame_sz);

    if (etherate.app_opt.sk_type == SKT_PACKET_MMAP) {
        printf("Using raw socket with PACKET_MMAP ring.\n"); ///// Add TPACKET version number
        tpacket_v2_ring(&etherate);
    } else if (etherate.app_opt.sk_type == SKT_PACKET) {
        printf("Using raw packet socket with send()/read().\n");
    } else if (etherate.app_opt.sk_type == SKT_SENDMSG) {
        printf("Using raw packet socket with sendmsg()/recvmsg().\n");
    } else if (etherate.app_opt.sk_type == SKT_SENDMMSG) {
        printf("Using raw packet socket with sendmmsg()/recvmmsg().\n");
    }

    if (etherate.app_opt.sk_mode == SKT_RX) {
        printf("Running in Rx mode.\n");
    } else if (etherate.app_opt.sk_mode == SKT_TX) {
        printf("Running in Tx mode.\n");
    } else if (etherate.app_opt.sk_mode == SKT_BIDI) {
        printf("Running in bidirectional mode.\n");
    }


    ///// Add Kernel version checks here for e.g TPACKETv3 +Tx or +Rx
    
    /////printf("Main is thread %" PRIu32 "\n", getpid());

    
    // Fill the test frame buffer with random data
    if (etherate.frm_opt.custom_frame == 0) {
        for (uint16_t i = 0; i < etherate.frm_opt.frame_sz; i += 1)
        {
            etherate.frm_opt.tx_buffer[i] = (uint8_t)((255.0*rand()/(RAND_MAX+1.0)));
        }
    }


    pthread_t worker_thread[etherate.app_opt.thd_cnt];
    pthread_attr_t worker_attr[etherate.app_opt.thd_cnt];


    // Create a copy of the program settings for each worker thread
    etherate.thd_opt = calloc(sizeof(struct thd_opt), etherate.app_opt.thd_cnt);

    for (uint16_t thread = 0; thread < etherate.app_opt.thd_cnt; thread += 1) {

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

        // Set up and copy per-thread settings
        etherate.thd_opt[thread].block_frm_sz    = etherate.frm_opt.block_frm_sz;
        etherate.thd_opt[thread].block_nr        = etherate.frm_opt.block_nr;
        etherate.thd_opt[thread].block_sz        = etherate.frm_opt.block_sz;
        etherate.thd_opt[thread].fanout_grp      = etherate.app_opt.fanout_grp;
        etherate.thd_opt[thread].frame_nr        = etherate.frm_opt.frame_nr;
        etherate.thd_opt[thread].frame_sz        = etherate.frm_opt.frame_sz;
        etherate.thd_opt[thread].frm_sz_max      = DEF_FRM_SZ_MAX;
        etherate.thd_opt[thread].if_index        = etherate.sk_opt.if_index;
        strncpy((char*)etherate.thd_opt[thread].if_name, (char*)etherate.sk_opt.if_name, IF_NAMESIZE);
        etherate.thd_opt[thread].mmap_buf        = NULL;
        etherate.thd_opt[thread].msgvec_vlen     = etherate.sk_opt.msgvec_vlen;
        etherate.thd_opt[thread].rd              = NULL;
        etherate.thd_opt[thread].rx_buffer       = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);
        etherate.thd_opt[thread].rx_bytes        = 0;
        etherate.thd_opt[thread].rx_pkts         = 0;
        etherate.thd_opt[thread].started         = 0; /////
        etherate.thd_opt[thread].sk_mode         = etherate.app_opt.sk_mode;
        etherate.thd_opt[thread].sk_type         = etherate.app_opt.sk_type;
        etherate.thd_opt[thread].thd_cnt         = etherate.app_opt.thd_cnt;
        etherate.thd_opt[thread].thd_idx         = thread;
        etherate.thd_opt[thread].tx_buffer       = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);
        memcpy(etherate.thd_opt[thread].tx_buffer, etherate.frm_opt.tx_buffer, DEF_FRM_SZ_MAX);
        etherate.thd_opt[thread].tx_bytes        = 0;
        etherate.thd_opt[thread].tx_pkts         = 0;
        etherate.thd_opt[thread].verbose         = etherate.app_opt.verbose;
        

        ///// Perhaps get the thread to set its own affinity first before it does anything else?
        if (etherate.app_opt.thd_sk_affin) {
            cpu_set_t current_cpu_set;

            int cpu_to_bind = thread % etherate.app_opt.thd_cnt;
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
        if (etherate.app_opt.sk_mode == SKT_TX) {
            if (etherate.app_opt.sk_type == SKT_PACKET_MMAP) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tx_tpacket_v2, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_PACKET) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tx_packet, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_SENDMSG) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tx_sendmsg, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_SENDMMSG) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], tx_sendmmsg, (void*)&etherate.thd_opt[thread]);
            }
        } else if (etherate.app_opt.sk_mode == SKT_RX) {
            if (etherate.app_opt.sk_type == SKT_PACKET_MMAP) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], rx_tpacket_v2, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_PACKET) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], rx_packet, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_SENDMSG) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], rx_recvmsg, (void*)&etherate.thd_opt[thread]);
            } else if (etherate.app_opt.sk_type == SKT_SENDMMSG) {
                worker_thread_ret = pthread_create(&worker_thread[thread], &worker_attr[thread], rx_recvmmsg, (void*)&etherate.thd_opt[thread]);
            }
        }

        if (worker_thread_ret) {
          printf("Return code from worker thread creation is %" PRIi32 "\n", worker_thread_ret);
          exit(EXIT_FAILURE);
        }

    }


    // Spawn a stats printing thread
    pthread_t stats_thread;
    int32_t stats_thread_ret = pthread_create(&stats_thread, NULL, print_pps, (void*)&etherate);


    // Free attribute and wait for the worker threads to finish
    for(uint16_t thread = 0; thread < etherate.app_opt.thd_cnt; thread += 1) {
        
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
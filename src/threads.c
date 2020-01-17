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



#include "threads.h"



static void thd_alloc(struct etherate *eth) {
    // thd_nr+1 to include the worker threads + the stats printing thread:
    eth->app_opt.thd = calloc(sizeof(pthread_t), (eth->app_opt.thd_nr + 1));
    eth->app_opt.thd_attr = calloc(sizeof(pthread_attr_t), (eth->app_opt.thd_nr + 1));
}



static void thd_cleanup(void *thd_opt_p) {

    struct thd_opt *thd_opt = thd_opt_p;

    if (thd_opt->quit)
        return;

    thd_opt->quit = 1;

    if (thd_opt->mmap_buf != NULL) {
        if (munmap(thd_opt->mmap_buf, (thd_opt->block_sz * thd_opt->block_nr)) != 0)
            tperror(thd_opt, "Can't free worker mmap buffer");
    }

    if (thd_opt->sock != 0) {
        if (close(thd_opt->sock) != 0)
            tperror(thd_opt, "Can't close worker socket");
    }

    free(thd_opt->err_str);
    free(thd_opt->ring);
    free(thd_opt->rx_buffer);
    free(thd_opt->tx_buffer);

}



static int32_t thd_init_stats(struct etherate *eth) {

    if (pthread_attr_init(&eth->app_opt.thd_attr[eth->app_opt.thd_nr]) != 0) {
        perror("Can't init stats thread attrs");
        return(EXIT_FAILURE);
    }
    if (pthread_attr_setdetachstate(&eth->app_opt.thd_attr[eth->app_opt.thd_nr], PTHREAD_CREATE_JOINABLE) != 0) {
        perror("Can't set stats thread detach state");
        return(EXIT_FAILURE);
    }
    if (pthread_create(&eth->app_opt.thd[eth->app_opt.thd_nr], &eth->app_opt.thd_attr[eth->app_opt.thd_nr], print_stats, (void*)eth) != 0) {
        perror("Can't create stats thread");
        return(EXIT_FAILURE);
    }
    if (pthread_attr_destroy(&eth->app_opt.thd_attr[eth->app_opt.thd_nr]) != 0) {
        perror("Can't remove stats thread attributes");
        return(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;

}



static int32_t thd_init_worker(struct etherate *eth, uint16_t thread) {

    if (eth->app_opt.sk_type == SKT_PACKET_MMAP2) {
        if (pthread_create(&eth->app_opt.thd[thread],
                           &eth->app_opt.thd_attr[thread],
                           tpacket_v2_init,
                           (void*)&eth->thd_opt[thread])
            != 0)
        {
            perror("Can't create worker thread");
            return(EXIT_FAILURE);
        }


    } else if (eth->app_opt.sk_type == SKT_PACKET) {
        if (pthread_create(&eth->app_opt.thd[thread],
                           &eth->app_opt.thd_attr[thread],
                           packet_init,
                           (void*)&eth->thd_opt[thread])
            != 0)
        {
            perror("Can't create worker thread");
            return(EXIT_FAILURE);
        }

    } else if (eth->app_opt.sk_type == SKT_SENDMSG) {
        if (pthread_create(&eth->app_opt.thd[thread],
                           &eth->app_opt.thd_attr[thread],
                           msg_init,
                           (void*)&eth->thd_opt[thread])
            != 0)
        {
            perror("Can't create worker thread");
            return(EXIT_FAILURE);
        }

    } else if (eth->app_opt.sk_type == SKT_SENDMMSG) {
        if (pthread_create(&eth->app_opt.thd[thread],
                           &eth->app_opt.thd_attr[thread],
                           mmsg_init,
                           (void*)&eth->thd_opt[thread])
            != 0)
        {
            perror("Can't create worker thread");
            return(EXIT_FAILURE);
        }

    } else if (eth->app_opt.sk_type == SKT_PACKET_MMAP3) {
        if (pthread_create(&eth->app_opt.thd[thread],
                           &eth->app_opt.thd_attr[thread],
                           tpacket_v3_init,
                           (void*)&eth->thd_opt[thread])
            != 0)
        {
            perror("Can't create worker thread");
            return(EXIT_FAILURE);
        }

    }

    if (pthread_attr_destroy(&eth->app_opt.thd_attr[thread]) != 0) {
        perror("Can't remove worker thread attributes");
        return(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}




static void thd_setup(struct etherate *eth, uint16_t thread) {

    // Set up thread local copies of all settings
    eth->thd_opt[thread].block_frm_sz    = eth->frm_opt.block_frm_sz;
    eth->thd_opt[thread].block_nr        = eth->frm_opt.block_nr;
    eth->thd_opt[thread].block_sz        = eth->frm_opt.block_sz;
    eth->thd_opt[thread].err_len         = eth->app_opt.err_len;
    eth->thd_opt[thread].err_str         = (char*)calloc(eth->app_opt.err_len, 1);
    eth->thd_opt[thread].fanout_grp      = eth->app_opt.fanout_grp;
    eth->thd_opt[thread].frame_nr        = eth->frm_opt.frame_nr;
    eth->thd_opt[thread].frame_sz        = eth->frm_opt.frame_sz;
    eth->thd_opt[thread].frm_sz_max      = DEF_FRM_SZ_MAX;
    eth->thd_opt[thread].if_index        = eth->sk_opt.if_index;
    strncpy((char*)eth->thd_opt[thread].if_name,
            (char*)eth->sk_opt.if_name,
            IF_NAMESIZE);
    eth->thd_opt[thread].mmap_buf        = NULL;
    eth->thd_opt[thread].msgvec_vlen     = eth->sk_opt.msgvec_vlen;
    eth->thd_opt[thread].quit            = 0;
    eth->thd_opt[thread].ring            = NULL;
    eth->thd_opt[thread].rx_buffer       = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);
    eth->thd_opt[thread].rx_bytes        = 0;
    eth->thd_opt[thread].rx_frms         = 0;
    eth->thd_opt[thread].started         = 0;
    eth->thd_opt[thread].sk_err          = 0;
    eth->thd_opt[thread].sk_mode         = eth->app_opt.sk_mode;
    eth->thd_opt[thread].sk_type         = eth->app_opt.sk_type;
    eth->thd_opt[thread].thd_nr          = eth->app_opt.thd_nr;
    eth->thd_opt[thread].thd_id          = 0;
    eth->thd_opt[thread].tx_buffer       = (uint8_t*)calloc(DEF_FRM_SZ_MAX,1);
    eth->thd_opt[thread].tx_bytes        = 0;
    eth->thd_opt[thread].tx_frms         = 0;
    eth->thd_opt[thread].verbose         = eth->app_opt.verbose;

    if (eth->thd_opt[thread].err_str == NULL   ||
        eth->thd_opt[thread].rx_buffer == NULL ||
        eth->thd_opt[thread].tx_buffer == NULL) {
        printf("Failed to calloc() per-thread buffers!\n");
        exit(EXIT_FAILURE);
    }

    // Copy the frame data into the thread local Tx buffer
    memcpy(eth->thd_opt[thread].tx_buffer, eth->frm_opt.tx_buffer, DEF_FRM_SZ_MAX);

}



static void tperror(struct thd_opt *thd_opt, const char *msg) {

    printf("%" PRIu32 ":%s (%d: %s)\n",
           thd_opt->thd_id, msg, errno,
           strerror_r(errno, thd_opt->err_str, thd_opt->err_len)
          );

}
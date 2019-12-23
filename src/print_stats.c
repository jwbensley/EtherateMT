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



#include "print_stats.h"



void *print_stats(void *etherate_p) {

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    struct   etherate *eth     = etherate_p;
    uint64_t duration          = 0;
    uint64_t rx_bytes          = 0;
    uint64_t rx_bytes_now      = 0;
    uint64_t rx_bytes_previous = 0;
    uint64_t rx_drops          = 0; // This is clear-on-read, delta since last read
    uint64_t rx_frms_now       = 0;
    uint64_t rx_frms_previous  = 0;
    uint64_t rx_pps            = 0;
    uint64_t rx_qfrz           = 0; // This is clear-on-read, delta since last read
    uint64_t tx_bytes          = 0;
    uint64_t tx_bytes_now      = 0;
    uint64_t tx_bytes_previous = 0;
    uint64_t tx_frms_now       = 0;
    uint64_t tx_frms_previous  = 0;
    uint64_t tx_pps            = 0;
    double   rx_gbps           = 0;
    double   tx_gbps           = 0;


    // Wait for one of the Tx/Rx threads to start
    uint8_t waiting = 1;
    while (waiting) {
        for(uint16_t thread = 0; thread < eth->app_opt.thd_nr; thread++) {
            if (eth->thd_opt[thread].started == 1) waiting = 0;
            /////if (eth->thd_opt[thread].quit == 1) pthread_exit((void*)EXIT_SUCCESS);
        }
    }


    // Wait for 1 second otherwise the first stats print will be all-zeros.
    sleep(1);


    while(1) {

        rx_bytes_now = 0;
        rx_drops     = 0;
        rx_frms_now  = 0;
        rx_qfrz      = 0;
        tx_bytes_now = 0;
        tx_frms_now  = 0;


        for(uint16_t thread = 0; thread < eth->app_opt.thd_nr; thread++) {

            // Check if the worker threads are still running
            if (eth->thd_opt[thread].quit == 1) pthread_exit((void*)EXIT_SUCCESS);

            rx_bytes_now += eth->thd_opt[thread].rx_bytes;
            rx_frms_now  += eth->thd_opt[thread].rx_frms;
            tx_bytes_now += eth->thd_opt[thread].tx_bytes;
            tx_frms_now  += eth->thd_opt[thread].tx_frms;


            if (eth->thd_opt[thread].stalling) {
                printf("%" PRIu32 ":Socket is stalling!\n", eth->thd_opt[thread].thd_id);
                eth->thd_opt[thread].stalling = 0;
            }


            // There are no special or different stats available in the Tx direction
            if(eth->thd_opt[thread].sk_mode == SKT_RX) {

                // struct tpacket_stats for TPACKET V2
                if (eth->app_opt.sk_mode == SKT_PACKET_MMAP2) {

                    tpacket_v2_stats(&eth->thd_opt[thread], &rx_drops);

                // struct tpacket_stats_v3 for TPACKET V3
                } else if (eth->app_opt.sk_mode == SKT_PACKET_MMAP3) {

                    tpacket_v3_stats(&eth->thd_opt[thread], &rx_drops, &rx_qfrz);

                }

            }

        }


        rx_bytes = rx_bytes_now - rx_bytes_previous;
        rx_drops = rx_drops;
        rx_qfrz  = rx_qfrz;
        rx_pps   = rx_frms_now  - rx_frms_previous;
        tx_bytes = tx_bytes_now - tx_bytes_previous;
        tx_pps   = tx_frms_now  - tx_frms_previous;

        rx_gbps = ((double)(rx_bytes*8)/1000/1000/1000);
        tx_gbps = ((double)(tx_bytes*8)/1000/1000/1000);

        if(eth->app_opt.verbose) {
            printf("%" PRIu64 ".\tRx: %.2f Gbps (%" PRIu64 " fps) %lu Drops %lu Q-Freeze\tTx: %.2f Gbps (%" PRIu64 " fps)\n",
                   duration, rx_gbps, rx_pps, rx_drops, rx_qfrz, tx_gbps, tx_pps);
        } else {
            printf("%" PRIu64 ".\tRx: %.2f Gbps (%" PRIu64 " fps)\tTx: %.2f Gbps (%" PRIu64 " fps)\n",
                   duration, rx_gbps, rx_pps, tx_gbps, tx_pps);
        }


        rx_bytes_previous = rx_bytes_now;
        rx_frms_previous  = rx_frms_now;
        tx_bytes_previous = tx_bytes_now;
        tx_frms_previous  = tx_frms_now;

        duration += 1;

        sleep(1);

        ///// On thread quit print min/max/avg in Gbps and pps, also total GBs/TBs transfered?

    } // while(1)
}
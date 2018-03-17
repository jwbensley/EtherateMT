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



// Worker thread entry function
void *tpacket_v3_init(void* thd_opt_p);

// TX/RX_RING ring/block alignment
void tpacket_v3_ring_align(struct thd_opt *thd_opt);

// TX/RX_RING init
void tpacket_v3_ring_init(struct thd_opt *thd_opt);

// PACKET_MMAP Rx thread loop
void tpacket_v3_rx(struct thd_opt *thd_opt);

// Return PACKET_MMAP socket FD
int32_t tpacket_v3_sock(struct thd_opt *thd_opt);

// TPACKET V3 Rx socket stats
void tpacket_v3_stats(struct thd_opt *thd_opt, uint64_t *rx_drops, uint64_t *rx_qfrz);

// PACKET_MMAP Tx thread loop
void tpacket_v3_tx(struct thd_opt *thd_opt);


struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
}; ///// Can this be removed or renamed

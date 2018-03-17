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



#define _GNU_SOURCE           // Required for pthread_attr_setaffinity_np()
#include <errno.h>            // errno
#include <net/ethernet.h>     // ETH_P_ALL
#include <net/if.h>           // IF_NAMESIZE, struct ifreq
#include <linux/if_packet.h>  // struct packet_mreq, sockaddr_ll, tpacket_req, tpacket2_hdr, tpacket3_hdr, tpacket_req3
#include <ifaddrs.h>          // freeifaddrs(), getifaddrs()
#include <arpa/inet.h>        // htons()
#include <inttypes.h>         // PRIuN
#include <sys/ioctl.h>        // ioctl()
#include <math.h>             // floor()
#include <sys/mman.h>         // mmap()
#include <linux/net_tstamp.h> // struct hwtstamp_config
#include <poll.h>             // poll()
#include <pthread.h>          // pthread_*()
#include <sys/socket.h>       // socket()
#include <linux/sockios.h>    // SIOCSHWTSTAMP
#include <stdlib.h>           // calloc(), exit(), EXIT_FAILURE, EXIT_SUCCESS, rand(), RAND_MAX, strtoul()
#include <stdio.h>            // FILE, fclose(), fopen(), fscanf(), perror(), printf()
#include <string.h>           // memcpy(), memset(), strncpy()
#include <sys/syscall.h>      // SYS_gettid
#include "sysexits.h"         // EX_NOPERM, EX_PROTOCOL, EX_SOFTWARE
#include <unistd.h>           // getpagesize(), getpid(), getuid(), read(), sleep()
#include <linux/version.h>    // KERNEL_VERSION(), LINUX_VERSION_CODE ///// Can we renmove this now?



// Global constants:
#define app_version "MT 0.6.beta 2018-03"



// Global defaults:
#define DEF_FRM_SZ     1514           // Default Ethernet frame size at layer 2 excluding FCS
#define DEF_BLK_FRM_SZ 2096           // Default frame size in a block, data + TPACKET2_HDRLEN (52).
#define DEF_BLK_SZ     getpagesize()  // Default block size
#define DEF_BLK_NR     256            // Default number of blocks per ring
#define DEF_ERR_LEN    128            // Default length of string from errno
#define DEF_FRM_SZ_MAX 10000          // Max frame size with headers
#define DEF_MSGVEC_LEN 256            // Default msgvec_vlen for sendmmsg()/recvmmsg()
#define DEF_THD_NR     1              // Default number of worker threads

// Flags for socket mode:
#define SKT_RX    0                   // Run in Rx mode
#define SKT_TX    1                   // Run in Tx mode
#define SKT_BIDI  2                   // Run in bidirectional mode (Tx and Rx)

// Flags for socket type:
#define SKT_PACKET        0           // Use read()/sendto()
#define SKT_PACKET_MMAP2  1           // Use PACKET_MMAP v2 Tx/Rx rings
#define SKT_SENDMSG       2           // Use sendmsg()/recvmsg()
#define SKT_SENDMMSG      3           // Use sendmmsg()/recvmmsg()
#define SKT_PACKET_MMAP3  4           // Use PACKET_MMAP v3 Tx/Rx rings
#define DEF_SKT_TYPE      SKT_PACKET  // Default mode



// Application behaviour options:
struct app_opt {
    uint8_t  err_len;
    char     *err_str;
    int32_t  fanout_grp;    
    uint8_t  sk_mode;
    uint8_t  sk_type;
    uint16_t thd_nr;
    uint8_t  thd_sk_affin; ///// Add CLI arg, try to avoid split NUMA node?
    uint8_t  verbose;
};

// Frame and ring buffer options:
struct frm_opt {
    uint32_t block_frm_sz; // Size of frame in block (frame_sz + TPACKET2_HDRLEN)
    uint32_t block_nr;     // Number of frame blocks per ring
    uint32_t block_sz;     // Size of frame block in ring
    uint8_t  custom_frame; // Bool to load a customer frame form file
    uint16_t frame_sz;     // Frame size (layer 2 headers + layer 2 payload)
    uint32_t frame_nr;     // Total number of frames in ring
    uint8_t  *tx_buffer;   // Point to frame copied into ring
};

// Socket specific options:
struct sk_opt {
    int32_t  if_index;
    uint8_t  if_name[IF_NAMESIZE];
    uint32_t msgvec_vlen;
};

// A copy of the values required for each thread
struct thd_opt {
    struct   sockaddr_ll bind_addr;
    uint32_t block_frm_sz;
    uint32_t block_nr;
    uint32_t block_sz;
    uint8_t  err_len;
    char     *err_str;
    uint32_t fanout_grp;
    uint32_t frame_nr;
    uint16_t frame_sz;
    uint16_t frm_sz_max;
    int32_t  if_index;
    uint8_t  if_name[IF_NAMESIZE];
    uint8_t* mmap_buf;
    uint32_t msgvec_vlen;
    struct   iovec* rd; ///// RENAME
    uint8_t  *rx_buffer;
    uint64_t rx_bytes;
    uint64_t rx_pkts;
    uint8_t  sk_mode;
    uint8_t  sk_type;
    int32_t  sock_fd;
    uint8_t  started;
    uint8_t  stalling;   // Socket is returning ENOBUFS
    uint16_t thd_nr; ///// Keep or remove?
    uint16_t thd_id;
    void     *tpacket_req3; // v3 for Rx ///// These need to be wrapped in a Kernel version check? Make them as pointers and move to local .c/.h files
    uint8_t  tpacket_req3_sz;
    void     *tpacket_req;  // v2 for Tx
    uint8_t  tpacket_req_sz;
    uint16_t ring_type;
    uint8_t  *tx_buffer;
    uint64_t tx_bytes;
    uint64_t tx_pkts;
    uint8_t  verbose;
};

struct etherate {
    struct   app_opt app_opt;
    struct   frm_opt frm_opt;
    struct   sk_opt sk_opt;
    struct   thd_opt *thd_opt;
};

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



#ifndef _SOCK_OP_H_
#define _SOCK_OP_H_

#define S_O_PROMISC_ADD 1
#define S_O_PROMISC_REM 2
#define S_O_BIND        3
#define S_O_QLEN        4
#define S_O_LOSSY       5
#define S_O_VER_TP      6
#define S_O_NIC_TS      7
#define S_O_TS          8
#define S_O_QDISC       9
#define S_O_RING_TP2    10
#define S_O_RING_TP3    11
#define S_O_MMAP_TP23   12
#define S_O_FANOUT      13



// Configure a socket option against thd_opt->sock_fd
int32_t sock_op(uint8_t op, struct thd_opt *thd_opt);

#endif // _SOCK_OP_H_
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



#ifndef _THREADS_H_
#define _THREADS_H_

// Alloc thread controls for all threads
static void thd_alloc(struct etherate *eth);

// Butch: "Thread's dead baby, Thread's dead"
static void thd_cleanup(void *thd_opt_p);

// Spawn the stats printing thread
static int32_t thd_init_stats(struct etherate *eth);

// Spawn a worker thread
static int32_t thd_init_worker(struct etherate *eth, uint16_t thread);

// Join the stats thread on exit
static void thd_join_stats(struct etherate *eth);

// Join worker threads on exit
static void thd_join_workers(struct etherate *eth);

// Copy settings into a new worker thread
static void thd_setup(struct etherate *eth, uint16_t thread);

// Print a custom message with the errno text and thread ID of the calling thread
static void tperror(struct thd_opt *thd_opt, const char *msg);

#endif // _THREADS_H_
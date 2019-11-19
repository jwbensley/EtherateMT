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


// Process CLI args
uint8_t cli_args(int argc, char *argv[], struct etherate *eth);

// Cleanup main etherate struct
void etherate_cleanup(struct etherate *eth);

// Populate settings with default values
void etherate_setup(struct etherate *eth);

// Return interface index from name
int32_t get_if_index_by_name(uint8_t if_name[IF_NAMESIZE]);

// List available AF_PACKET interfaces and their interface index
void get_if_list();

// Copy interface name from interface index number into char*
void get_if_name_by_index(int32_t if_index, uint8_t* if_name);

// Print CLI usage/args
void print_usage();

// Remove the interface from promiscuous mode
int16_t rem_int_promisc(struct etherate *eth);

// Set the interface an interface in promiscuous mode
int16_t set_int_promisc(struct etherate *eth);

// Signal handler to clean up threads
void signal_handler(int signal);

// Worker thread cleanup
void thread_cleanup(void *thd_opt_p);

// Set the default settings for a worker thread
void thread_init(struct etherate *eth, uint16_t thread);

// Print a custom message with the errno text and thread ID of the calling thread
void tperror(struct thd_opt *thd_opt, const char *msg);

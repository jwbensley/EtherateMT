/*
 * License: MIT
 *
 * Copyright (c) 2016-2017 James Bensley.
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
uint8_t cli_args(int argc, char *argv[], struct etherate *etherate);

// Populate settings with default values
void etherate_setup(struct etherate *etherate);

// Return interface index from name
int32_t get_if_index_by_name(uint8_t if_name[IF_NAMESIZE]);

// List available AF_PACKET interfaces and their interface index
void get_if_list();

// Copy interface name from interface index number into char*
void get_if_name_by_index(int32_t if_index, uint8_t* if_name);

// Total and print traffic stats every second
void *print_pps(void *etherate_p);

// Print CLI usage/args
void print_usage ();

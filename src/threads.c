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



static int32_t spawn_stats_thd(struct etherate *eth) {

    if (pthread_attr_init(&eth->app_opt.thd_attr[eth->app_opt.thd_nr]) != 0) {
        perror("Can't init stats thread attrs");
        return(EXIT_FAILURE);
    }
    if (pthread_attr_setdetachstate(&eth->app_opt.thd_attr[eth->app_opt.thd_nr], PTHREAD_CREATE_JOINABLE) != 0) {
        perror("Can't set stats thread detach state");
        return(EXIT_FAILURE);
    }
    if (pthread_create(&eth->app_opt.thd[eth->app_opt.thd_nr], &eth->app_opt.thd_attr[eth->app_opt.thd_nr], print_stats, (void*)&eth) != 0) {
        perror("Can't create stats thread");
        return(EXIT_FAILURE);
    }
    if (pthread_attr_destroy(&eth->app_opt.thd_attr[eth->app_opt.thd_nr]) != 0) {
        perror("Can't remove stats thread attributes");
        return(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;

}



static void thd_init(struct etherate *eth) {
    // thd_nr+1 to include the worker threads + stats printing thread:
    eth->app_opt.thd = calloc(sizeof(pthread_t), (eth->app_opt.thd_nr + 1));
    eth->app_opt.thd_attr = calloc(sizeof(pthread_attr_t), (eth->app_opt.thd_nr + 1));
}
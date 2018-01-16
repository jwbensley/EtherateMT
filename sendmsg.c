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



uint16_t frame_count = 21; // What limits this?
struct msghdr msg_hdr;
struct iovec iov[frame_count];
memset(&msg_hdr, 0, sizeof(msg_hdr));
memset(iov, 0, sizeof(iov));

for (int i = 0; i < frame_count; i += 1) {
    //memcpy(&iov[i].iov_base, frame_headers->tx_buffer, test_params->f_size_total);
    iov[i].iov_base = frame_headers->tx_buffer;
    iov[i].iov_len = test_params->f_size_total;
}

msg_hdr.msg_iov = iov;
msg_hdr.msg_iovlen = frame_count;


                tx_ret = sendmsg(test_interface->sock_fd, &msg_hdr, 0); //// MSG_DONTWAIT ?

                if (tx_ret <= 0)
                {
                    perror("Speed test Tx error ");
                    return;
                }


                test_params->f_tx_count += frame_count;
                speed_test->b_tx += (test_params->f_size_total * frame_count);
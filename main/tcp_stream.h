/**
 * MIT License
 *
 * Copyright (c) 2019 Jiameng Shi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _TCP_STREAM_H_
#define _TCP_STREAM_H_

#include "lwip/sockets.h"
#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tcp_stream_context tcp_stream_context_t, *tcp_stream_context_handle_t;
typedef struct tcp_stream tcp_stream_t, *tcp_stream_handle_t;

struct tcp_stream {
	void *context;
	bool (*open)(tcp_stream_handle_t s, char *hostname, int port);
    bool (*close)(tcp_stream_handle_t s);
    int (*read)(tcp_stream_handle_t s, char *buffer, int bufsz);
    int (*write)(tcp_stream_handle_t s, char *buffer, int bufsz); 
};

tcp_stream_handle_t tcp_stream_create(void);
void tcp_stream_destroy(tcp_stream_handle_t);
void tcp_stream_set_timeout(tcp_stream_handle_t, struct timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif /* _TCP_STREAM_H_ */

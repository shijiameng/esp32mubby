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

#ifndef _STREAMER_H_
#define _STREAMER_H_

#include "lwip/sockets.h"
#include "lwip/err.h"
#include <openssl/ssl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _streamer;
typedef struct _streamer *streamer_handle_t;

struct _streamer {
	int sock;
	SSL_CTX *ctx;
    SSL *ssl;
    bool is_open;
    bool (*open)(streamer_handle_t s, char *hostname, int port);
    bool (*close)(streamer_handle_t s);
    int (*read)(streamer_handle_t s, char *buffer, int bufsz);
    int (*write)(streamer_handle_t s, char *buffer, int bufsz); 
};

streamer_handle_t streamer_create(void);
void streamer_destroy(streamer_handle_t);
void streamer_set_timeout(streamer_handle_t, struct timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif /* _STREAMER_H_ */

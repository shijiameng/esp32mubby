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

#include "sdkconfig.h"
#include "streamer.h"

static bool streamer_open(streamer_handle_t s, char *hostname, int port)
{
	struct sockaddr_in addr;
	
	s->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (s->sock < 0) {
		return false;
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((short)port);
	
	if (inet_pton(AF_INET, hostname, &addr.sin_addr) < 0) {
		close(s->sock);
		return false;
	}
	
	if (connect(s->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(s->sock);
		return false;
	}
	
	s->is_open = true;
	
	SSL_set_fd(s->ssl, s->sock);
	
	if (SSL_connect(s->ssl) < 0) {		
		return false;
	}
	
	return true;
}

static bool streamer_close(streamer_handle_t s)
{
	if (s && s->is_open) {
		SSL_shutdown(s->ssl);
		close(s->sock);
		s->sock = -1;
		s->is_open = false;
		return true;
	}
	
	return false;
}

static int streamer_read(streamer_handle_t s, char *buffer, int bufsz)
{
	return SSL_read(s->ssl, buffer, bufsz);
}

static int streamer_write(streamer_handle_t s, char *buffer, int bufsz)
{
	return SSL_write(s->ssl, buffer, bufsz);
}

streamer_handle_t streamer_create(void)
{
	streamer_handle_t s;
	
	s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	
	s->sock = -1;
	s->is_open = false;
	s->ctx = SSL_CTX_new(TLSv1_1_client_method());
	s->ssl = SSL_new(s->ctx);
	
	s->open = streamer_open;
	s->close = streamer_close;
	s->read = streamer_read;
	s->write = streamer_write;
	
	return s;
}

void streamer_destroy(streamer_handle_t s)
{
	if (s) {
		if (s->ssl) {
			SSL_free(s->ssl);
		}
		if (s->ctx) {
			SSL_CTX_free(s->ctx);
		}
		free(s);
	}
}

void streamer_set_timeout(streamer_handle_t s, struct timeval *timeout)
{
	setsockopt(s->sock, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(struct timeval));
}

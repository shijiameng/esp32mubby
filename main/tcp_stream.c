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
#include "tcp_stream.h"

#ifdef CONFIG_ENABLE_SECURITY_PROTO
#include <openssl/ssl.h>
#endif

struct tcp_stream_context {
	int sock;
    bool is_open;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#endif
};

static bool tcp_stream_open(tcp_stream_handle_t s, char *hostname, int port)
{
	tcp_stream_context_handle_t ctx = s->context;
	struct sockaddr_in addr;
	
	ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (ctx->sock < 0) {
		return false;
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((short)port);
	
	if (inet_pton(AF_INET, hostname, &addr.sin_addr) < 0) {
		close(ctx->sock);
		return false;
	}
	
	if (connect(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(ctx->sock);
		return false;
	}
	
	ctx->is_open = true;
	
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	SSL_set_fd(ctx->ssl, ctx->sock);
	
	if (SSL_connect(ctx->ssl) < 0) {		
		return false;
	}
#endif
	
	return true;
}

static bool tcp_stream_close(tcp_stream_handle_t s)
{
	if (s) {
		tcp_stream_context_handle_t ctx = s->context;
		if (ctx && ctx->is_open) {
#ifdef CONFIG_ENABLE_SECURITY_PROTO
			SSL_shutdown(ctx->ssl);
#endif
			close(ctx->sock);
			ctx->sock = -1;
			ctx->is_open = false;
			return true;
		}
	}
	
	return false;
}

static int tcp_stream_read(tcp_stream_handle_t s, char *buffer, int bufsz)
{
	tcp_stream_context_handle_t ctx = s->context;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	return SSL_read(ctx->ssl, buffer, bufsz);
#else
	return recv(ctx->sock, buffer, bufsz, 0);
#endif
}

static int tcp_stream_write(tcp_stream_handle_t s, char *buffer, int bufsz)
{
	tcp_stream_context_handle_t ctx = s->context;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	return SSL_write(ctx->ssl, buffer, bufsz);
#else
	return send(ctx->sock, buffer, bufsz, 0);
#endif
}

tcp_stream_handle_t tcp_stream_create(void)
{
	tcp_stream_handle_t s;
	tcp_stream_context_handle_t ctx;
	
	s = calloc(1, sizeof(struct tcp_stream));
	if (!s) {
		return NULL;
	}
	
	ctx = calloc(1, sizeof(struct tcp_stream_context));
	if (!ctx) {
		free(s);
		return NULL;
	}

	ctx->sock = -1;
	ctx->is_open = false;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	ctx->ssl_ctx = SSL_CTX_new(TLSv1_1_client_method());
	ctx->ssl = SSL_new(ctx->ssl_ctx);
#endif
	s->context = ctx;
	s->open = tcp_stream_open;
	s->close = tcp_stream_close;
	s->read = tcp_stream_read;
	s->write = tcp_stream_write;
	
	return s;
}

void tcp_stream_destroy(tcp_stream_handle_t s)
{
	tcp_stream_context_handle_t ctx = s->context;
	if (s) {
#ifdef CONFIG_ENABLE_SECURITY_PROTO
		if (ctx->ssl) {
			SSL_free(ctx->ssl);
		}
		if (ctx->ssl_ctx) {
			SSL_CTX_free(ctx->ssl_ctx);
		}
#endif
		free(ctx);
		free(s);
	}
}

void tcp_stream_set_timeout(tcp_stream_handle_t s, struct timeval *timeout)
{
	tcp_stream_context_handle_t ctx = s->context;
	setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(struct timeval));
}

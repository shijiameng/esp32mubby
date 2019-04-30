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
#include "mubby.h"
#include "esp_log.h"

#ifdef CONFIG_ENABLE_SECURITY_PROTO
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#endif

struct tcp_stream_context {
#ifdef CONFIG_ENABLE_SECURITY_PROTO
    mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_x509_crt cacert;
	mbedtls_x509_crt clntcert;
	mbedtls_pk_context clntkey;
	mbedtls_ssl_config conf;
	mbedtls_net_context server_fd;
#else
	int sock;
#endif
	bool is_open;
};

static const char *TAG = "STREAM";

static bool tcp_stream_open(tcp_stream_handle_t s, char *hostname, int port)
{
	tcp_stream_context_handle_t ctx = s->context;
	
	ESP_LOGI(TAG, "Connecting to %s:%u...", hostname, (uint16_t)port);
	
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	int flags, ret;
	char str_port[6] = {0};
	
	snprintf(str_port, sizeof(str_port), "%u", (uint16_t)port);
	
	mbedtls_net_init(&ctx->server_fd);

	if ((ret = mbedtls_net_connect(&ctx->server_fd, hostname,
								  str_port, MBEDTLS_NET_PROTO_TCP)) != 0) {
		ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
		return false;
	}

	ESP_LOGI(TAG, "Connected.");

	mbedtls_ssl_set_bio(&ctx->ssl, &ctx->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

	while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
			return false;
		}
	}

	ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

	if ((flags = mbedtls_ssl_get_verify_result(&ctx->ssl)) != 0) {
		/* In real life, we probably want to close connection if ret != 0 */
		ESP_LOGW(TAG, "Failed to verify peer certificate!");
		return false;
	} else {
		ESP_LOGI(TAG, "Certificate verified.");
	}

	ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(&ctx->ssl));
	
#else
	
	struct sockaddr_in addr;
	
	ctx->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (ctx->sock < 0) {
		return false;
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	
	if (inet_pton(AF_INET, hostname, &addr.sin_addr) < 0) {
		close(ctx->sock);
		return false;
	}
	
	if (connect(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(ctx->sock);
		return false;
	}
#endif

	ctx->is_open = true;
	
	return true;
}

static bool tcp_stream_close(tcp_stream_handle_t s)
{
	if (s) {
		tcp_stream_context_handle_t ctx = s->context;
		if (ctx && ctx->is_open) {
#ifdef CONFIG_ENABLE_SECURITY_PROTO
			mbedtls_ssl_session_reset(&ctx->ssl);
			mbedtls_net_free(&ctx->server_fd);
#else
			close(ctx->sock);
			ctx->sock = -1;
#endif
			ctx->is_open = false;
			return true;
		}
	}
	
	return false;
}

static int tcp_stream_read(tcp_stream_handle_t s, void *buffer, int bufsz)
{
	tcp_stream_context_handle_t ctx = s->context;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	return mbedtls_ssl_read(&ctx->ssl, buffer, bufsz);
#else
	return recv(ctx->sock, buffer, bufsz, 0);
#endif
}

static int tcp_stream_write(tcp_stream_handle_t s, const void *buffer, int bufsz)
{
	tcp_stream_context_handle_t ctx = s->context;
#ifdef CONFIG_ENABLE_SECURITY_PROTO
	return mbedtls_ssl_write(&ctx->ssl, buffer, bufsz);
#else
	return send(ctx->sock, buffer, bufsz, 0);
#endif
}


/**
 * @brief Create a TCP stream
 * @return TCP stream handle on success, NULL otherwise
 */
tcp_stream_handle_t tcp_stream_create(void)
{
	tcp_stream_handle_t s;
	tcp_stream_context_handle_t ctx;
	int ret;
	
	s = calloc(1, sizeof(struct tcp_stream));
	if (!s) {
		return NULL;
	}
	
	ctx = calloc(1, sizeof(struct tcp_stream_context));
	if (!ctx) {
		free(s);
		return NULL;
	}

#ifdef CONFIG_ENABLE_SECURITY_PROTO
	mbedtls_ssl_init(&ctx->ssl);
    mbedtls_x509_crt_init(&ctx->cacert);
    mbedtls_x509_crt_init(&ctx->clntcert);
    mbedtls_pk_init(&ctx->clntkey);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
	mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_entropy_init(&ctx->entropy);
    
	if ((ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy, NULL, 0)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
		return NULL;
	}

    ESP_LOGI(TAG, "Loading the CA root certificate...");

	ret = mbedtls_x509_crt_parse(&ctx->cacert, cacert_pem_start,
								 strlen((char *)cacert_pem_start) + 1);
	if (ret < 0) {
		ESP_LOGE(TAG, "Failed to parse CA root certificate ret=-0x%x\n\n", -ret);
		return NULL;
	}
	
	ESP_LOGI(TAG, "Loading the client certificate...");
	
	ret = mbedtls_x509_crt_parse(&ctx->clntcert, cert_pem_start, strlen((char *)cert_pem_start) + 1);
	if (ret < 0) {
		ESP_LOGE(TAG, "Failed to parse client certificate, ret=-0x%x", -ret);
		return NULL;
	}
	
	ESP_LOGI(TAG, "Loading the client private key...");
	
	ret = mbedtls_pk_parse_key(&ctx->clntkey, privkey_pem_start, strlen((char *)privkey_pem_start) + 1, NULL, 0);
	if (ret < 0) {
		ESP_LOGE(TAG, "Failed to parse private key, ret=-0x%x", -ret);
		return NULL;
	}
	
	ESP_LOGI(TAG, "Setting the client certificate...");
	
	ret = mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->clntcert, &ctx->clntkey);
	if (ret < 0) {
		ESP_LOGE(TAG, "Failed to set client certificate, ret=-0x%x", -ret);
		return NULL;
	}
	
	ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

	if ((ret = mbedtls_ssl_config_defaults(&ctx->conf,
										  MBEDTLS_SSL_IS_CLIENT,
										  MBEDTLS_SSL_TRANSPORT_STREAM,
										  MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
		return NULL;
	}
	
	mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->cacert, NULL);
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

	if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf)) != 0) {
		ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
		return NULL;
	}
#else
	ctx->sock = -1;
#endif
	ctx->is_open = false;

	s->context = ctx;
	s->open = tcp_stream_open;
	s->close = tcp_stream_close;
	s->read = tcp_stream_read;
	s->write = tcp_stream_write;
	
	return s;
}

/**
 * @brief Destroy a TCP stream
 * @param [in] s The TCP stream handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t tcp_stream_destroy(tcp_stream_handle_t s)
{
	tcp_stream_context_handle_t ctx = s->context;
	if (s && ctx) {
		if (ctx->is_open) {
			s->close(s);
		}
		free(ctx);
		free(s);
		return ESP_OK;
	}
	
	return ESP_FAIL;
}

/**
 * @brief Set a timeout for the TCP I/O
 * @param [in] s 	The TCP stream handle
 * @param [in] ms 	The timeout in milliseconds
 */
void tcp_stream_set_timeout(tcp_stream_handle_t s, unsigned int ms)
{
	tcp_stream_context_handle_t ctx = s->context;
#ifndef CONFIG_ENABLE_SECURITY_PROTO
	struct timeval timeout = {ms / 1000, (ms % 1000) * 1000};
	setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
#else
	mbedtls_ssl_conf_read_timeout(&ctx->conf, (uint32_t)ms);
	mbedtls_ssl_set_bio(&ctx->ssl, &ctx->server_fd, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
#endif
}

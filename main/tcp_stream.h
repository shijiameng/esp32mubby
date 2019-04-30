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
	/**
	 * @brief The private data of TCP stream
	 */
	void *context;
	
	/**
	 * @brief Open a TCP stream
	 * @param [in] s 			The TCP stream handle
	 * @param [in] hostname 	The server hostname
	 * @param [in] port			The server port
	 * @return true on success, false on error
	 */
	bool (*open)(tcp_stream_handle_t s, char *hostname, int port);
    
    /**
     * @brief Close a TCP stream
     * @param [in] s The TCP stream handle
     * @return true on success, false on error
     */
    bool (*close)(tcp_stream_handle_t s);
    
    /**
     * @brief Read a data block from the TCP stream
     * @param [in]  s		The TCP stream handle
     * @param [out]	buffer	The buffer in which the data will be saved
     * @param [in]	bufsz	The buffer size
     * @return The number of bytes read, -1 on error
     */
    int (*read)(tcp_stream_handle_t s, void *buffer, int bufsz);
    
    /**
     * @brief Write a data block to the TCP stream
     * @param [in] s		The TCP stream handle
     * @param [in] buffer	The buffer in which the data will be written
     * @param [in] bufsz	The buffer size
     * @return The number of bytes written, -1 on error
     */
    int (*write)(tcp_stream_handle_t s, const void *buffer, int bufsz); 
};

/**
 * @brief Create a TCP stream
 * @return TCP stream handle on success, NULL otherwise
 */
tcp_stream_handle_t tcp_stream_create(void);

/**
 * @brief Destroy a TCP stream
 * @param [in] s The TCP stream handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t tcp_stream_destroy(tcp_stream_handle_t s);

/**
 * @brief Set a timeout for the TCP I/O
 * @param [in] s 	The TCP stream handle
 * @param [in] ms 	The timeout in milliseconds
 */
void tcp_stream_set_timeout(tcp_stream_handle_t s, unsigned int ms);

#ifdef __cplusplus
}
#endif

#endif /* _TCP_STREAM_H_ */

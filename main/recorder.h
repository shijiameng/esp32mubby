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

#ifndef _RECORDER_H_
#define _RECORDER_H_

#include "tcp_stream.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_STATE_STARTED	(0)
#define RECORDER_STATE_FINISHED (1)
#define RECORDER_STATE_ABORTED	(2)
#define RECORDER_STATE_ERROR	(3)

typedef struct audio_recorder *audio_recorder_handle_t;


/**
 * @brief Create a recorder
 * @return recorder handle on success, NULL otherwise
 */
audio_recorder_handle_t recorder_create(void);


/**
 * @brief Destroy a recorder
 * @param [in] ar The recorder handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t recorder_destroy(audio_recorder_handle_t ar);


/**
 * @brief Start the recorder
 * @param [in] ar The recorder handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t recorder_start(audio_recorder_handle_t ar);


/**
 * @brief Stop the recorder
 * @param [in] ar The recorder handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t recorder_stop(audio_recorder_handle_t ar);


/**
 * @brief Set an event listener
 * @param [in] ar 	The recorder handle
 * @param [in] evt 	The event listener handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t recorder_set_event_listener(audio_recorder_handle_t ar, audio_event_iface_handle_t evt);


/**
 * @brief Set a data stream for the recorder
 * @param [in] ar		The recorder handle
 * @param [in] stream	The TCP stream
 */
esp_err_t recorder_set_tcp_stream(audio_recorder_handle_t ar, tcp_stream_handle_t stream);

#ifdef __cplusplus
}
#endif

#endif /* _RECORDER_H_ */

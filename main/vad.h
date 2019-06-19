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
 
#ifndef _VAD_H_
#define _VAD_H_

#include "tcp_stream.h"
#include "audio_event_iface.h"
 
#ifdef __cplusplus
extern "C" {
#endif

#define VAD_STATE_STARTED	(0)
#define VAD_STATE_FINISHED (1)
#define VAD_STATE_ABORTED	(2)
#define VAD_STATE_ERROR	(3)

typedef struct audio_voice_detector *audio_voice_detector_handle_t;


/**
 * @brief Create a voice_detector
 * @return voice_detector handle on success, NULL otherwise
 */
audio_voice_detector_handle_t voice_detector_create(void);


/**
 * @brief Destroy a voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_destroy(audio_voice_detector_handle_t ar);


/**
 * @brief Start the voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_start(audio_voice_detector_handle_t ar);


/**
 * @brief Stop the voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_stop(audio_voice_detector_handle_t ar);


/**
 * @brief Set an event listener
 * @param [in] ar 	The voice_detector handle
 * @param [in] evt 	The event listener handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_set_event_listener(audio_voice_detector_handle_t ar, audio_event_iface_handle_t evt);


/**
 * @brief Set a data stream for the voice_detector
 * @param [in] ar		The voice_detector handle
 * @param [in] stream	The TCP stream
 */
esp_err_t voice_detector_set_tcp_stream(audio_voice_detector_handle_t ar, tcp_stream_handle_t stream);
 
#ifdef __cplusplus
}
#endif
 
#endif /* _VAD_H_ */

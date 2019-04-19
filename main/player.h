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
 
#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "tcp_stream.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_STATE_STARTED	(0)
#define PLAYER_STATE_FINISHED	(1)
#define PLAYER_STATE_ABORTED	(2)
#define PLAYER_STATE_ERROR		(3)

typedef struct audio_player *audio_player_handle_t;


/**
 * @brief Create a player
 * @return player handle on success, NULL otherwise
 */
audio_player_handle_t player_create(void);


/**
 * @brief Destroy a player
 * @param [in] ap The player handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t player_destroy(audio_player_handle_t ap);


/**
 * @brief Start playing a MP3 stream
 * @param [in] ap The player handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t player_start(audio_player_handle_t ap);


/**
 * @brief Stop playing a MP3 stream
 * @param [in] ap The player handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t player_stop(audio_player_handle_t ap);


/**
 * @brief Set an event listener
 * @param [in] ap 	The player handle
 * @param [in] evt 	The event listener handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t player_set_event_listener(audio_player_handle_t ap, audio_event_iface_handle_t evt);


/**
 * @brief Set a data stream for the player
 * @param [in] ap		The player handle
 * @param [in] stream	The TCP stream handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t player_set_tcp_stream(audio_player_handle_t ap, tcp_stream_handle_t stream);

#ifdef __cplusplus
}
#endif
 
#endif /* _PLAYER_H_ */

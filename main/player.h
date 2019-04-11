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

typedef struct audio_player *audio_player_handle_t;

audio_player_handle_t player_create(void);
esp_err_t player_destroy(audio_player_handle_t ap);
esp_err_t player_start(audio_player_handle_t ap);
esp_err_t player_stop(audio_player_handle_t ap);
esp_err_t player_set_event_listener(audio_player_handle_t ap, audio_event_iface_handle_t evt);
audio_event_iface_handle_t player_get_event_iface(audio_player_handle_t ap);
esp_err_t player_set_tcp_stream(audio_player_handle_t ap, tcp_stream_handle_t stream);

#ifdef __cplusplus
}
#endif
 
#endif /* _PLAYER_H_ */

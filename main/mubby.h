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

#ifndef _MUBBY_H_
#define _MUBBY_H_

#include "player.h"
#include "recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MUBBY_ID_CORE			(0)
#define MUBBY_ID_PLAYER			(1)
#define MUBBY_ID_RECORDER		(2)
#define MUBBY_ID_WIFIMGR		(3)

typedef enum {
	MUBBY_STATE_RESET = -1,
	MUBBY_STATE_STANDBY = 0,
	MUBBY_STATE_CONNECTING,
	MUBBY_STATE_RECORDING,
	MUBBY_STATE_STOP_RECORDING,
	MUBBY_STATE_RECORDING_FINISHED,
	MUBBY_STATE_PLAYING,
	MUBBY_STATE_PLAYING_FINISHED,
	MUBBY_STATE_SHUTDOWN
} mubby_state_t;


struct app_context {
	audio_player_handle_t 		ap;
	audio_recorder_handle_t 	ar;
	audio_event_iface_handle_t 	evt;
	tcp_stream_handle_t			stream;
	uint8_t						macaddr[6];
	QueueHandle_t 				msg_queue;
	mubby_state_t 				cur_state;
	bool						cnt_chat;
};

typedef struct app_context *app_context_handle_t;
typedef struct app_context app_context_t;

extern unsigned char cacert_pem_start[] asm("_binary_cacert_pem_start");
extern unsigned char cacert_pem_end[] asm("_binary_cacert_pem_end");
extern unsigned char cert_pem_start[] asm("_binary_cert_pem_start");
extern unsigned char cert_pem_end[] asm("_binary_cert_pem_end");
extern unsigned char privkey_pem_start[] asm("_binary_privkey_pem_start");
extern unsigned char privkey_pem_end[] asm("_binary_privkey_pem_end");

#ifdef __cplusplus
}
#endif

#endif /* _MUBBY_H_ */

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

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/err.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "board.h"

#include "mubby.h"
#include "recorder.h"

#define RECORDER_TASK_SIZE			4096
#define RECORDER_TASK_PRIORITY		5

#define RECORDER_BUFSIZE 			8192

#define RECORDER_IDLE_BIT			BIT0
#define RECORDER_PROCESSING_BIT		BIT2
#define RECORDER_START_BIT			BIT3
#define RECORDER_STOP_BIT			BIT4

static const char *TAG = "RECORDER";

struct audio_recorder {
	TaskHandle_t 					task;
	app_context_handle_t			app_ctx;
	audio_event_iface_handle_t 		external_event;
	audio_event_iface_handle_t 		internal_event;
	audio_element_handle_t 			i2s_stream_reader;
	tcp_stream_handle_t				stream;
	bool							is_running;
};

extern int g_server_sockfd;
extern audio_board_handle_t g_board_handle;

static int recorder_write_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	tcp_stream_handle_t stream = (tcp_stream_handle_t)ctx;
	int write_len = stream->write(stream, buf, len);
	return write_len;
}

static esp_err_t recorder_notify_sync(audio_recorder_handle_t ar, int state)
{
	audio_event_iface_msg_t msg;
	
	msg.source_type = MUBBY_ID_RECORDER;
	msg.data = (void *)state;
	
	return audio_event_iface_sendout(ar->external_event, &msg);
}

static void recorder_task(void *pvParameters)
{
	audio_recorder_handle_t ar = (audio_recorder_handle_t)pvParameters;
	
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	
	audio_event_iface_set_listener(ar->internal_event, evt);
    
	audio_element_run(ar->i2s_stream_reader);
	audio_element_resume(ar->i2s_stream_reader, 0, 0);
	
	ar->is_running = true;
	recorder_notify_sync(ar, RECORDER_STATE_STARTED);
	
	for (;;) {
		audio_event_iface_msg_t msg = {0};
		
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
			continue;
		}
		
		if (msg.source_type == MUBBY_ID_CORE) {
			if (!strncmp((char *)msg.data, "stop", 4)) {
				ESP_LOGW(TAG, "[ * ] Interrupted externally");
				break;
			}
		}
	}
	
	ar->is_running = false;
	
	audio_event_iface_remove_listener(evt, ar->internal_event);
	audio_element_terminate(ar->i2s_stream_reader);
	audio_event_iface_destroy(evt);

	recorder_notify_sync(ar, RECORDER_STATE_FINISHED);
	
	vTaskDelete(NULL);
}

audio_recorder_handle_t recorder_create(void)
{
	audio_recorder_handle_t ar;
	
	ar = calloc(1, sizeof(struct audio_recorder));
	if (!ar) {
		return NULL;
	}
	
	audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	ar->external_event = audio_event_iface_init(&cfg);
	mem_assert(ar->external_event);
	
	ar->internal_event = audio_event_iface_init(&cfg);
	mem_assert(ar->internal_event);
		
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.i2s_config.sample_rate = 8000;
	i2s_cfg.type = AUDIO_STREAM_READER;
	ar->i2s_stream_reader = i2s_stream_init(&i2s_cfg);
	mem_assert(ar->i2s_stream_reader);
	
	ar->is_running = false;
	
	return ar;
}

esp_err_t recorder_destroy(audio_recorder_handle_t ar)
{	
	return ESP_OK;
}

esp_err_t recorder_start(audio_recorder_handle_t ar)
{
	if (xTaskCreate(recorder_task, "recorder_task", RECORDER_TASK_SIZE, (void *)ar, RECORDER_TASK_PRIORITY, &ar->task) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create recorder task");
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

esp_err_t recorder_stop(audio_recorder_handle_t ar)
{
	if (ar->is_running) {
		audio_event_iface_msg_t msg = {
			.source_type = MUBBY_ID_CORE,
			.data = (void *)"stop"
		};

		return audio_event_iface_sendout(ar->internal_event, &msg);
	}
	
	return ESP_OK;
}

esp_err_t recorder_set_event_listener(audio_recorder_handle_t ar, audio_event_iface_handle_t evt)
{
	return audio_event_iface_set_listener(ar->external_event, evt);
}

audio_event_iface_handle_t recorder_get_event_iface(audio_recorder_handle_t ar)
{
	return ar->external_event;
}

esp_err_t recorder_set_tcp_stream(audio_recorder_handle_t ar, tcp_stream_handle_t stream)
{
	ar->stream = stream;
	return audio_element_set_write_cb(ar->i2s_stream_reader, recorder_write_cb, ar->stream);
}

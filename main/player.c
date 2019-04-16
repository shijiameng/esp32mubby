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
#include "audio_common.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "board.h"

#include "mubby.h"
#include "player.h"
 
static const char *TAG = "PLAYER";

#define PLAYER_TASK_SIZE		4096
#define PLAYER_TASK_PRIORITY	5

#define PLAYER_BUFSIZE 			8192

#define PLAYER_IDLE_BIT			BIT0
#define PLAYER_PROCESSING_BIT	BIT2
#define PLAYER_START_BIT		BIT3
#define PLAYER_STOP_BIT			BIT4

extern int g_server_sockfd;
extern audio_board_handle_t g_board_handle;

struct audio_player {
	TaskHandle_t					task;
	app_context_handle_t			app_ctx;
	audio_event_iface_handle_t 		external_event;
	audio_event_iface_handle_t 		internal_event;
	audio_element_handle_t 			i2s_stream_writer;
	audio_element_handle_t 			mp3_decoder;
	audio_pipeline_handle_t 		pipeline;
	tcp_stream_handle_t				stream;
	bool 							is_running;	
};

static esp_err_t player_notify_sync(audio_player_handle_t ap, int state)
{
	audio_event_iface_msg_t msg = {0};
	
	msg.source_type = MUBBY_ID_PLAYER;
	msg.data = (void *)state;
	
	return audio_event_iface_sendout(ap->external_event, &msg);
}

static int player_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	tcp_stream_handle_t stream = (tcp_stream_handle_t)ctx;
	
	int read_len = stream->read(stream, buf, len);
	if (read_len == 0 || (read_len == -1 && errno == EAGAIN)) {
		read_len = AEL_IO_DONE;
	}

	return read_len;
}

static void player_task(void *pvParameters)
{
	audio_player_handle_t ap = (audio_player_handle_t)pvParameters;
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	
	audio_event_iface_set_listener(ap->internal_event, evt);
	audio_pipeline_set_listener(ap->pipeline, evt);
	audio_pipeline_run(ap->pipeline);
	
	player_notify_sync(ap, PLAYER_STATE_STARTED);
	ap->is_running = true;
	
	for (;;) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
			continue;
		}
        
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)ap->mp3_decoder
			&& msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
			audio_element_info_t music_info = {0};
			audio_element_getinfo(ap->mp3_decoder, &music_info);
			
			ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rate=%d, bits=%d, ch=%d",
						music_info.sample_rates, music_info.bits, music_info.channels);
			
			audio_element_setinfo(ap->i2s_stream_writer, &music_info);
			
			i2s_stream_set_clk(ap->i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
			
			continue;
		}
		
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)ap->i2s_stream_writer
			&& msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_STATE_STOPPED) {
			ESP_LOGW(TAG, "[ * ] Stop event received");
			break;
		}
        
		if (msg.source_type == MUBBY_ID_CORE) {
			printf("stopping...\n");
			if (!strncmp((char *)msg.data, "stop", 4)) {
				ESP_LOGW(TAG, "[ * ] Interrupted externally");
				break;
			}
		}
	}
	
	ap->is_running = false;
	
	audio_pipeline_terminate(ap->pipeline);
	audio_event_iface_remove_listener(evt, ap->internal_event);
	audio_pipeline_remove_listener(ap->pipeline);
	audio_event_iface_destroy(evt);
	
	player_notify_sync(ap, PLAYER_STATE_FINISHED);
	
	vTaskDelete(NULL);
}

audio_player_handle_t player_create(void)
{
	audio_player_handle_t ap;
	ap = calloc(1, sizeof(struct audio_player));
	if (!ap) {
		return NULL;
	}
	
	audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	ap->external_event = audio_event_iface_init(&cfg);
	mem_assert(ap->external_event);
	
	ap->internal_event = audio_event_iface_init(&cfg);
	mem_assert(ap->internal_event);
	
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type = AUDIO_STREAM_WRITER;
	ap->i2s_stream_writer = i2s_stream_init(&i2s_cfg);
	mem_assert(ap->i2s_stream_writer);
	
	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	ap->mp3_decoder = mp3_decoder_init(&mp3_cfg);
	mem_assert(ap->mp3_decoder);
	
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	ap->pipeline = audio_pipeline_init(&pipeline_cfg);
	mem_assert(ap->pipeline);

	audio_pipeline_register(ap->pipeline, ap->mp3_decoder, "mp3");
	audio_pipeline_register(ap->pipeline, ap->i2s_stream_writer, "i2s");
	audio_pipeline_link(ap->pipeline, (const char *[]){"mp3", "i2s"}, 2);
	
	ap->is_running = false;
	
	return ap;
}

esp_err_t player_destroy(audio_player_handle_t ap)
{
	return ESP_OK;
}

esp_err_t player_start(audio_player_handle_t ap)
{
	if (xTaskCreate(player_task, "player_task", PLAYER_TASK_SIZE, (void *)ap, PLAYER_TASK_PRIORITY, &ap->task) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create player task");
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

esp_err_t player_stop(audio_player_handle_t ap)
{
	if (ap->is_running) {
		audio_event_iface_msg_t msg = {
			.source_type = MUBBY_ID_CORE,
			.data = (void *)"stop"
		};
		
		return audio_event_iface_sendout(ap->internal_event, &msg);
	}
	
	return ESP_OK;
}

esp_err_t player_set_event_listener(audio_player_handle_t ap, audio_event_iface_handle_t evt)
{
	return audio_event_iface_set_listener(ap->external_event, evt);
}

audio_event_iface_handle_t player_get_event_iface(audio_player_handle_t ap)
{
	return ap->external_event;
}

esp_err_t player_set_tcp_stream(audio_player_handle_t ap, tcp_stream_handle_t stream)
{
	ap->stream = stream;
	return audio_element_set_read_cb(ap->mp3_decoder, player_read_cb, ap->stream);
}

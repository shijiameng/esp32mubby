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
#include "streamer.h"
#include "player.h"
 
static const char *TAG = "PLAYER";

#define PLAYER_TASK_SIZE		5120
#define PLAYER_TASK_PRIORITY	5

#define PLAYER_BUFSIZE 			8192

#define PLAYER_IDLE_BIT			BIT0
#define PLAYER_PROCESSING_BIT	BIT2
#define PLAYER_START_BIT		BIT3
#define PLAYER_STOP_BIT			BIT4

static TaskHandle_t xPlayerTask = NULL;
static audio_event_iface_handle_t s_ext_evt_iface = NULL;
static audio_event_iface_handle_t s_int_evt_iface = NULL;
static audio_element_handle_t i2s_stream_writer = NULL;
static audio_element_handle_t mp3_decoder = NULL;
static audio_pipeline_handle_t pipeline;
static bool is_running = false;

extern int g_server_sockfd;
extern audio_board_handle_t g_board_handle;

static esp_err_t player_notify_sync(int state)
{
	audio_event_iface_msg_t msg = {0};
	
	msg.source_type = MUBBY_ID_PLAYER;
	msg.data = (void *)state;
	
	return audio_event_iface_sendout(s_ext_evt_iface, &msg);
}

static int player_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	int read_len = recv(g_server_sockfd, buf, len, 0);
	if (read_len == 0 || (read_len == -1 && errno == EAGAIN)) {
		read_len = AEL_IO_DONE;
	}

	return read_len;
}

static void player_task(void *pvParameters)
{
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	
	audio_event_iface_set_listener(s_int_evt_iface, evt);
	audio_pipeline_set_listener(pipeline, evt);
	audio_pipeline_run(pipeline);
	
	player_notify_sync(PLAYER_STATE_STARTED);
	is_running = true;
	
	for (;;) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
			continue;
		}
        
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)mp3_decoder
			&& msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
			audio_element_info_t music_info = {0};
			audio_element_getinfo(mp3_decoder, &music_info);
			
			ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rate=%d, bits=%d, ch=%d",
						music_info.sample_rates, music_info.bits, music_info.channels);
			
			audio_element_setinfo(i2s_stream_writer, &music_info);
			
			i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
			
			continue;
		}
		
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
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
	
	is_running = false;
	
	audio_pipeline_terminate(pipeline);
	audio_event_iface_remove_listener(evt, s_int_evt_iface);
	audio_pipeline_remove_listener(pipeline);
	audio_event_iface_destroy(evt);
	
	player_notify_sync(PLAYER_STATE_FINISHED);
	
	vTaskDelete(NULL);
}

esp_err_t player_create(void)
{
	audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	s_ext_evt_iface = audio_event_iface_init(&cfg);
	mem_assert(s_ext_evt_iface);
	
	s_int_evt_iface = audio_event_iface_init(&cfg);
	mem_assert(s_int_evt_iface);
	
	ESP_LOGI(TAG, "[ 1 ] Create i2s stream to write data to codec chip");
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type = AUDIO_STREAM_WRITER;
	i2s_stream_writer = i2s_stream_init(&i2s_cfg);
	
	ESP_LOGI(TAG, "[ 2 ] Create mp3 decoder to decode mp3 data");
	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	mp3_decoder = mp3_decoder_init(&mp3_cfg);
	
	ESP_LOGI(TAG, "[ 3 ] Associate custom read callback to mp3 decoder");
	audio_element_set_read_cb(mp3_decoder, player_read_cb, NULL);
	
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	pipeline = audio_pipeline_init(&pipeline_cfg);
	mem_assert(pipeline);
	
	ESP_LOGI(TAG, "[ 4 ] Register all elements to audio pipeline");
	audio_pipeline_register(pipeline, mp3_decoder, "mp3");
	audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");
	audio_pipeline_link(pipeline, (const char *[]){"mp3", "i2s"}, 2);
	
	is_running = false;
	
	return ESP_OK;
}

esp_err_t player_destroy(void)
{
	return ESP_OK;
}

esp_err_t player_start(void)
{
	if (xTaskCreate(player_task, "player_task", PLAYER_TASK_SIZE, NULL, PLAYER_TASK_PRIORITY, &xPlayerTask) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create player task");
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

esp_err_t player_stop(void)
{
	if (is_running) {
		audio_event_iface_msg_t msg = {
			.source_type = MUBBY_ID_CORE,
			.data = (void *)"stop"
		};
		
		return audio_event_iface_sendout(s_int_evt_iface, &msg);
	}
	
	return ESP_OK;
}

audio_event_iface_handle_t player_get_event_iface(void)
{
	return s_ext_evt_iface;
}

void player_set_streamer(streamer_handle_t s)
{
	audio_element_setdata(mp3_decoder, (void *)s);
}

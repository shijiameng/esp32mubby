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

static TaskHandle_t xRecorderTask = NULL;
static audio_event_iface_handle_t s_recorder_event_iface = NULL;
static audio_element_handle_t i2s_stream_reader = NULL;
static QueueHandle_t i2s_queue = NULL;
static QueueHandle_t recorder_queue = NULL;
static QueueSetHandle_t queue_set = NULL;

extern int g_server_sockfd;
extern audio_board_handle_t g_board_handle;

static int recorder_write_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	int write_len = send(g_server_sockfd, buf, len, 0);
	printf("write_len=%d\n", write_len);
	return write_len;
}

static esp_err_t recorder_notify_sync(int state)
{
	audio_event_iface_msg_t msg;;
	
	msg.source_type = MUBBY_ID_RECORDER;
	msg.data = (void *)state;
	
	return audio_event_iface_sendout(s_recorder_event_iface, &msg);
}

static void recorder_task(void *pvParameters)
{
	/**
	 * Struct:
	 * i2s_stream_reader -> ringbuf1 -> filter
	 */
	QueueSetMemberHandle_t queue_set_member = NULL;
	audio_event_iface_msg_t msg;
    
	audio_element_run(i2s_stream_reader);
	audio_element_resume(i2s_stream_reader, 0, 0);
	
	recorder_notify_sync(RECORDER_STATE_STARTED);
	
	while ((queue_set_member = xQueueSelectFromSet(queue_set, portMAX_DELAY))) {
		xQueueReceive(queue_set_member, &msg, portMAX_DELAY);
		
		if (queue_set_member == recorder_queue) {
			if (!strncmp((char *)msg.data, "stop", strlen((char *)msg.data))) {
				break;
			}
		}
	}
	
	audio_element_terminate(i2s_stream_reader);
	
	recorder_notify_sync(RECORDER_STATE_FINISHED);
	
	vTaskDelete(NULL);
}

esp_err_t recorder_create(void)
{
	audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	s_recorder_event_iface = audio_event_iface_init(&cfg);
	if (!s_recorder_event_iface) {
		ESP_LOGE(TAG, "Failed to create event interface");
		return ESP_FAIL;
	}
	
	ESP_LOGI(TAG, "[ 1 ] Create i2s stream to read audio data from codec chip");
		
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.i2s_config.sample_rate = 8000;
	i2s_cfg.type = AUDIO_STREAM_READER;
	i2s_stream_reader = i2s_stream_init(&i2s_cfg);
	
	ESP_LOGI(TAG, "[ 2 ] Associate custom write callback to filter");
	audio_element_set_write_cb(i2s_stream_reader, recorder_write_cb, NULL);
	
	i2s_queue = audio_element_get_event_queue(i2s_stream_reader);
	recorder_queue = xQueueCreate(10, sizeof(audio_event_iface_msg_t));
	
	queue_set = xQueueCreateSet(10);
	xQueueAddToSet(i2s_queue, queue_set);
	xQueueAddToSet(recorder_queue, queue_set);
	
	return ESP_OK;
}

esp_err_t recorder_destroy(void)
{	
	return ESP_OK;
}

esp_err_t recorder_start(void)
{
	if (xTaskCreate(recorder_task, "recorder_task", RECORDER_TASK_SIZE, NULL, RECORDER_TASK_PRIORITY, &xRecorderTask) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create recorder task");
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

esp_err_t recorder_stop(void)
{
	audio_event_iface_msg_t msg = {0};
	msg.data = (void *)"stop";
	xQueueSend(recorder_queue, &msg, portMAX_DELAY);
	return ESP_OK;
}

audio_event_iface_handle_t recorder_get_event_iface(void)
{
	return s_recorder_event_iface;
}

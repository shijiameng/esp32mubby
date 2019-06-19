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
#include "esp_vad.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_common.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "board.h"

#include "mubby.h"
#include "vad.h"

#define VAD_TASK_SIZE			4096
#define VAD_TASK_PRIORITY		5

#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

static const char *TAG = "VAD";

struct audio_voice_detector {
	TaskHandle_t 					task;
	app_context_handle_t			app_ctx;
	audio_event_iface_handle_t 		external_event;
	audio_event_iface_handle_t 		internal_event;
	audio_element_handle_t 			i2s_stream_reader;
	audio_element_handle_t			filter;
	audio_element_handle_t			raw_read;
	audio_pipeline_handle_t			pipeline;
	tcp_stream_handle_t				stream;
	bool							is_running;
};

static int voice_detector_write_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	tcp_stream_handle_t stream = (tcp_stream_handle_t)ctx;
	int write_len = stream->write(stream, buf, len);
	return write_len;
}

static esp_err_t voice_detector_notify_sync(audio_voice_detector_handle_t av, int state)
{
	audio_event_iface_msg_t msg;
	
	msg.source_type = MUBBY_ID_VAD;
	msg.data = (void *)state;
	
	return audio_event_iface_sendout(av->external_event, &msg);
}

static void voice_detector_task(void *pvParameters)
{
	audio_voice_detector_handle_t av = (audio_voice_detector_handle_t)pvParameters;
	
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	
	audio_event_iface_set_listener(av->internal_event, evt);
    
	
	/* notify the main task voice_detector is starting now */
	voice_detector_notify_sync(av, VAD_STATE_STARTED);
	av->is_running = true;
	
	audio_pipeline_run(av->pipeline);
	
	vad_handle_t vad_inst = vad_create(VAD_MODE_4, VAD_SAMPLE_RATE_HZ, VAD_FRAME_LENGTH_MS);
	
	int16_t *vad_buff = (int16_t *)malloc(VAD_BUFFER_LENGTH * sizeof(short));
	mem_assert(vad_buff);
	
	for (;;) {
		audio_event_iface_msg_t msg = {0};
		
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
			continue;
		}
		
		/* voice_detector received the stop instruction from the external */
		if (msg.source_type == MUBBY_ID_CORE) {
			if (!strncmp((char *)msg.data, "stop", 4)) {
				ESP_LOGW(TAG, "[ * ] Interrupted externally");
				break;
			}
		}
		
		raw_stream_read(av->raw_read, (char *)vad_buff, VAD_BUFFER_LENGTH * sizeof(short));
		
		vad_state_t vad_state = vad_process(vad_inst, vad_buff);
		if (vad_state == VAD_SPEECH) {
			// TODO: connect to server, send vad_buff to server
		}
	}
	
	av->is_running = false;
	
	audio_event_iface_remove_listener(evt, av->internal_event);
	audio_pipeline_terminate(av->pipeline);
	audio_event_iface_destroy(evt);

	voice_detector_notify_sync(av, VAD_STATE_FINISHED);
	
	free(vad_buff);
	vad_buff = NULL;
	
	vTaskDelete(NULL);
}

/**
 * @brief Create a voice_detector
 * @return voice_detector handle on success, NULL otherwise
 */
audio_voice_detector_handle_t voice_detector_create(void)
{
	audio_voice_detector_handle_t av;
	
	av = calloc(1, sizeof(struct audio_voice_detector));
	if (!av) {
		return NULL;
	}
	
	/* Create the external and internal event interface */
	audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	av->external_event = audio_event_iface_init(&cfg);
	mem_assert(av->external_event);
	av->internal_event = audio_event_iface_init(&cfg);
	mem_assert(av->internal_event);
	
	/* Create the I2S reader stream */
	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.i2s_config.sample_rate = 8000;
	i2s_cfg.type = AUDIO_STREAM_READER;
	av->i2s_stream_reader = i2s_stream_init(&i2s_cfg);
	mem_assert(av->i2s_stream_reader);
	
	raw_stream_cfg_t raw_cfg = {
		.out_rb_size = 8 * 1024,
		.type = AUDIO_STREAM_READER,
	};
	av->raw_read = raw_stream_init(&raw_cfg);
	
	audio_pipeline_register(av->pipeline, av->i2s_stream_reader, "i2s");
	audio_pipeline_register(av->pipeline, av->raw_read, "raw");
	audio_pipeline_link(av->pipeline, (const char *[]){"i2s", "raw"}, 2);
	
	av->is_running = false;
	
	return av;
}

/**
 * @brief Destroy a voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_destroy(audio_voice_detector_handle_t av)
{	
	vad_destroy(vad_inst);
	audio_pipeline_terminate(av->pipeline);
	
	return ESP_OK;
}

/**
 * @brief Start the voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_start(audio_voice_detector_handle_t av)
{
	if (xTaskCreate(voice_detector_task, "voice_detector_task", VAD_TASK_SIZE, (void *)av, VAD_TASK_PRIORITY, &av->task) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create voice_detector task");
		return ESP_FAIL;
	}
	
	return ESP_OK;
}

/**
 * @brief Stop the voice_detector
 * @param [in] ar The voice_detector handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_stop(audio_voice_detector_handle_t av)
{
	if (av->is_running) {
		audio_event_iface_msg_t msg = {
			.source_type = MUBBY_ID_CORE,
			.data = (void *)"stop"
		};

		return audio_event_iface_sendout(av->internal_event, &msg);
	}
	
	return ESP_OK;
}

/**
 * @brief Set an event listener
 * @param [in] ar 	The voice_detector handle
 * @param [in] evt 	The event listener handle
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t voice_detector_set_event_listener(audio_voice_detector_handle_t av, audio_event_iface_handle_t evt)
{
	return audio_event_iface_set_listener(av->external_event, evt);
}

/**
 * @brief Set a data stream for the voice_detector
 * @param [in] ar		The voice_detector handle
 * @param [in] stream	The TCP stream
 */
esp_err_t voice_detector_set_tcp_stream(audio_voice_detector_handle_t av, tcp_stream_handle_t stream)
{
	av->stream = stream;
	return audio_element_set_write_cb(av->i2s_stream_reader, voice_detector_write_cb, av->stream);
}


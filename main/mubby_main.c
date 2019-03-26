/* Play an MP3 file from HTTP

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/err.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_sdcard.h"
#include "filter_resample.h"

#include "esp_peripherals.h"
#include "board.h"

#include "http_server.h"
#include "wifi_manager.h"

#define BUFSIZE	8192

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

static QueueHandle_t s_pending_states; 
static TaskHandle_t xRecorderTask, xPlayerTask;
static TaskHandle_t task_http_server = NULL;
static TaskHandle_t task_wifi_manager = NULL;

static audio_board_handle_t s_board_handle;
static esp_mqtt_client_handle_t s_mqtt_client;

static int s_server_sockfd = -1;
static bool s_continue_chat = false;
static mubby_state_t s_mubby_state;

esp_periph_set_handle_t g_periph_set;

static EventGroupHandle_t s_recorder_event_group;

#define RECORDER_START		BIT0
#define RECORDER_STOP		BIT1

static EventGroupHandle_t s_player_event_group;

#define PLAYER_START		BIT0
#define PLAYER_STOP			BIT1

static StackType_t http_server_stack[2048];
static StackType_t wifi_manager_stack[4096];
static StaticTask_t http_server_task_buffer;
static StaticTask_t wifi_manager_task_buffer;

static int tcp_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	int read_len = recv(s_server_sockfd, buf, len, 0);
	if (read_len == 0) {
		read_len = AEL_IO_DONE;
	}

	return read_len;
}

static int tcp_write_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{
	int write_len = send(s_server_sockfd, buf, len, 0);
	return write_len;
}

static inline void push_state(mubby_state_t state)
{
	xQueueSend(s_pending_states, (void *)&state, portMAX_DELAY);
}

static esp_err_t msg_parser(char *msg)
{
	const char *TAG = "MQTT_CLIENT";
	esp_err_t ret = ESP_OK;
	cJSON *root, *header;
	
	root = cJSON_Parse(msg);
	if (!root) {
		ESP_LOGE(TAG, "Failed to parse message");
		ret = ESP_FAIL;
		goto errout;
	}
	
	header = cJSON_GetObjectItem(root, "header");
	if (!header) {
		ESP_LOGE(TAG, "Failed to parse 'header'");
		ret = ESP_ERR_INVALID_ARG;
		goto errout;
	}
	
	if (!strcmp(header->valuestring, "chat")) {
		cJSON *cont = cJSON_GetObjectItem(root, "continue");
		if (cont && !strcmp(cont->valuestring, "true")) {
			s_continue_chat = true;
		} else {
			s_continue_chat = false;
		}
		send(s_server_sockfd, (char []){'e', 'n', 'd'}, 3, 0);
		push_state(MUBBY_STATE_PLAYING);
	} else if (!strcmp(header->valuestring, "control")) {
		cJSON *sub = cJSON_GetObjectItem(root, "sub");
		if (!sub) {
			ESP_LOGE(TAG, "Failed to parse 'sub'");
			ret = ESP_ERR_INVALID_ARG;
			goto errout;
		}
		
		cJSON *part = cJSON_GetObjectItem(sub, "part");
		cJSON *act = cJSON_GetObjectItem(sub, "action");
		
		if (!part || !act) {
			ESP_LOGE(TAG, "Failed to parse 'sub'");
			ret = ESP_ERR_INVALID_ARG;
			goto errout;
		}
		
		if (!strcmp(part->valuestring, "volume")) {
			// TODO: adjust volume
		} else if (!strcmp(part->valuestring, "stt")) {
			if (!strcmp(act->valuestring, "end")) {
				//xEventGroupSetBits(s_recorder_event_group, RECORDER_STOP);
				push_state(MUBBY_STATE_STOP_RECORDING);
			} else {
				ESP_LOGE(TAG, "Invalid action '%s' for 'stt'", act->valuestring);
				ret = ESP_ERR_INVALID_ARG;
				goto errout;
			}
		} else {
			ESP_LOGE(TAG, "Invalid control part '%s'", part->valuestring);
			ret = ESP_ERR_INVALID_ARG;
			goto errout;
		}
	} else {
		ESP_LOGE(TAG, "Invalid header '%s'", header->valuestring);
		ret = ESP_ERR_INVALID_ARG;
		goto errout;
	}
	
errout:
	if (root) {
		cJSON_Delete(root);
	}
	return ret;
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	const char *TAG = "MQTT_CLIENT";
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	
	switch (event->event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		msg_id = esp_mqtt_client_subscribe(client, "mubby/client/soosang", 0);
		ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		break;
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MOTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");
		printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
		printf("DATA=%.*s\r\n", event->data_len, event->data);
		msg_parser(event->data);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		break;
	default:
		break;
	}
	
	return ESP_OK;
}

static void vRecorderTask(void *pvParameters)
{
	/**
	 * Struct:
	 * i2s_stream_reader -> ringbuf1 -> filter
	 */
	const char *TAG = "RECORDER";
	audio_element_handle_t i2s_stream_reader, filter;
	ringbuf_handle_t ringbuf;
	mubby_state_t next_state = MUBBY_STATE_RECORDING_FINISHED;
	
	ESP_LOGI(TAG, "Recorder created");
	
	for (;;) {
		xEventGroupWaitBits(s_recorder_event_group, RECORDER_START, pdTRUE, pdTRUE, portMAX_DELAY);
		
		ESP_LOGI(TAG, "----- Start recording ----");		
		ESP_LOGI(TAG, "[ 0 ] Start codec chip");
		
		audio_hal_ctrl_codec(s_board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_START);
			
		ESP_LOGI(TAG, "[ 1 ] Create i2s stream to read audio data from codec chip");
		
		i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
		i2s_cfg.i2s_config.sample_rate = 48000;
		i2s_cfg.type = AUDIO_STREAM_READER;
		i2s_stream_reader = i2s_stream_init(&i2s_cfg);
		
		ESP_LOGI(TAG, "[ 2 ] Create filter to resample audio data");
		rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
		rsp_cfg.src_rate = 48000;
		rsp_cfg.src_ch = 2;
		rsp_cfg.dest_rate = 16000;
		rsp_cfg.dest_ch = 1;
		rsp_cfg.type = AUDIO_CODEC_TYPE_ENCODER;
		filter = rsp_filter_init(&rsp_cfg);
		
		ESP_LOGI(TAG, "[ 3 ] Associate custom write callback to filter");
		audio_element_set_write_cb(filter, tcp_write_cb, NULL);
		
		ESP_LOGI(TAG, "[ 4 ] Create a ringbuffer between i2s_stream_reader and filter");
		ringbuf = rb_create(BUFSIZE, 1);
		audio_element_set_output_ringbuf(i2s_stream_reader, ringbuf);
		audio_element_set_input_ringbuf(filter, ringbuf);
		
		ESP_LOGI(TAG, "[ 5 ] Start audio elements");
		
		if (send(s_server_sockfd, (char []){'r', 'e', 'c'}, 3, 0) < 0) {
			ESP_LOGE(TAG, "Failed to send 'rec' to server");
			next_state = MUBBY_STATE_RESET;
			goto errout;
		}
		
		audio_element_run(i2s_stream_reader);
		audio_element_run(filter);
		
		audio_element_resume(i2s_stream_reader, 0, 0);
		audio_element_resume(filter, 0, 0);
		
		xEventGroupWaitBits(s_recorder_event_group, RECORDER_STOP, pdTRUE, pdTRUE, portMAX_DELAY);
		
		//ESP_LOGI(TAG, "Sending 'end' string");
		//int ret;
		//if ((ret = send(s_server_sockfd, (char []){'e', 'n', 'd'}, 3, 0)) != 3) {
			//ESP_LOGE(TAG, "Failed to send 'end' to server");
			//next_state = MUBBY_STATE_RESET;
		//}
		//ESP_LOGE(TAG, "ret=%d", ret);
		
		audio_element_terminate(i2s_stream_reader);
		audio_element_terminate(filter);
		
errout:
		audio_element_deinit(i2s_stream_reader);
		audio_element_deinit(filter);
		
		rb_destroy(ringbuf);

		//push_state(next_state);
		
		ESP_LOGI(TAG, "----- Recording stopped ----");
	}
}

static void vPlayerTask(void *pvParameters)
{
	const char *TAG = "PLAYER";
	audio_element_handle_t i2s_stream_writer, mp3_decoder;
	ringbuf_handle_t ringbuf;
	QueueHandle_t i2s_queue, mp3_queue;
	QueueSetHandle_t queue_set;
	QueueSetMemberHandle_t queue_set_member;
	
	ESP_LOGI(TAG, "Player created");
	
	for (;;) {
		xEventGroupWaitBits(s_player_event_group, PLAYER_START, pdTRUE, pdTRUE, portMAX_DELAY);
		
		ESP_LOGI(TAG, "----- Start playing back ----");
		ESP_LOGI(TAG, "[ 0 ] Start codec chip");
		audio_hal_ctrl_codec(s_board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
		
		ESP_LOGI(TAG, "[ 1 ] Create i2s stream to write data to codec chip");
		i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
		i2s_cfg.type = AUDIO_STREAM_WRITER;
		i2s_stream_writer = i2s_stream_init(&i2s_cfg);
		
		ESP_LOGI(TAG, "[ 2 ] Create mp3 decoder to decode mp3 data");
		mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
		mp3_decoder = mp3_decoder_init(&mp3_cfg);
		
		ESP_LOGI(TAG, "[ 3 ] Associate custom read callback to mp3 decoder");
		audio_element_set_read_cb(mp3_decoder, tcp_read_cb, NULL);
		
		ESP_LOGI(TAG, "[ 4 ] Create a ringbuffer and insert it between mp3 decoder and i2s writer");
		ringbuf = rb_create(BUFSIZE, 1);
		
		audio_element_set_input_ringbuf(i2s_stream_writer, ringbuf);
		audio_element_set_output_ringbuf(mp3_decoder, ringbuf);
		
		i2s_queue = audio_element_get_event_queue(i2s_stream_writer);
		mp3_queue = audio_element_get_event_queue(mp3_decoder);
		
		queue_set = xQueueCreateSet(10);
		xQueueAddToSet(i2s_queue, queue_set);
		xQueueAddToSet(mp3_queue, queue_set);
		
		ESP_LOGI(TAG, "[ 5 ] Start audio elements");
		audio_element_run(i2s_stream_writer);
		audio_element_run(mp3_decoder);
		
		audio_element_resume(i2s_stream_writer, 0, 0);
		audio_element_resume(mp3_decoder, 0, 0);
		
		esp_mqtt_client_publish(s_mqtt_client, "mubby/server/soosang", "{\"state\": \"ok\"}", 0, 0, 0);
		
		audio_event_iface_msg_t msg;
		while ((queue_set_member = xQueueSelectFromSet(queue_set, portMAX_DELAY))) {
			xQueueReceive(queue_set_member, &msg, portMAX_DELAY);
			
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
			
			if ((queue_set_member == i2s_queue || queue_set_member == mp3_queue) && 
				(int)msg.data == AEL_STATUS_STATE_STOPPED) {
				break;
			}
			
			if ((queue_set_member == i2s_queue || queue_set_member == mp3_queue) && 
				(int)msg.data == AEL_STATUS_ERROR_PROCESS) {
				break;
			}
		}
		
		if (xQueueRemoveFromSet(i2s_queue, queue_set) != pdPASS) {
			ESP_LOGE(TAG, "failed to remove i2s_queue from queue_set");
		}
		
		if (xQueueRemoveFromSet(mp3_queue, queue_set) != pdPASS) {
			ESP_LOGE(TAG, "failed to remove mp3_queue from queue_set");
		}
		
		vQueueDelete(i2s_queue);
		vQueueDelete(queue_set);

		audio_element_terminate(i2s_stream_writer);
		audio_element_terminate(mp3_decoder);
		audio_element_deinit(i2s_stream_writer);
		audio_element_deinit(mp3_decoder);
		
		rb_destroy(ringbuf);
		
		push_state(MUBBY_STATE_PLAYING_FINISHED);
		
		ESP_LOGI(TAG, "----- Stop playing back ----");
	}
}

static void vEventTask(void *pvParameters)
{
	const char *TAG = "EVENT_MONITOR";
	bool pushed = false;
	
	ESP_LOGI(TAG, "EventMonitor created");
	
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
	
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(g_periph_set), evt);
	
	for (;;) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
		
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Event interface error: %d", ret);
			continue;
		}
		
		if (msg.source_type != PERIPH_ID_BUTTON) {
			continue;
		}
		
		if ((int)msg.data == GPIO_NUM_36) {	
			if (msg.cmd == PERIPH_BUTTON_PRESSED) {
				pushed = true;
			} else if (pushed && (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE)) {
				pushed = false;
				if (s_mubby_state == MUBBY_STATE_STANDBY) {
					push_state(MUBBY_STATE_CONNECTING);
				}				
			}
		}
	}
}

#if 0
static void vMainTask(void *pvParameters)
{
	const char *TAG = "CORE";
	
	s_board_handle = audio_board_init();
	
	ESP_LOGI(TAG, "Core created");
	
	for (;;) {
		xQueueReceive(s_pending_states, (void *)&s_mubby_state, portMAX_DELAY);
		switch (s_mubby_state) {
		case MUBBY_STATE_RESET:
			ESP_LOGI(TAG, "MUBBY_STATE_RESET");
			if (s_server_sockfd != -1) {
				close(s_server_sockfd);
				s_server_sockfd = -1;
			}
			push_state(MUBBY_STATE_STANDBY);
			break;
		
		case MUBBY_STATE_STANDBY:
			ESP_LOGI(TAG, "MUBBY_STATE_STANDBY");
			break;
			
		case MUBBY_STATE_CONNECTING:
			if (s_server_sockfd == -1) {
				struct sockaddr_in server_addr;
				s_server_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				configASSERT(s_server_sockfd > 0);
				memset(&server_addr, 0, sizeof(server_addr));
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = htons(CONFIG_SERVER_PORT);
				server_addr.sin_addr.s_addr = inet_addr(CONFIG_SERVER_HOST);
				ESP_LOGI(TAG, "Connecting to server...");
				if (connect(s_server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
					ESP_LOGE(TAG, "Failed to connect server");
					push_state(MUBBY_STATE_RESET);
					continue;
				}
				ESP_LOGI(TAG, "Connection established");
				push_state(MUBBY_STATE_RECORDING);
			} else {
				ESP_LOGE(TAG, "Connection has already established");
				push_state(MUBBY_STATE_RESET);
			}
			break;
			
		case MUBBY_STATE_RECORDING:
			ESP_LOGI(TAG, "MUBBY_STATE_RECORDING");
			xEventGroupSetBits(s_recorder_event_group, RECORDER_START);
			break;
			
		case MUBBY_STATE_STOP_RECORDING:
			ESP_LOGI(TAG, "MUBBY_STATE_STOP_RECORDING");
			xEventGroupSetBits(s_recorder_event_group, RECORDER_STOP);
			break;
			
		case MUBBY_STATE_RECORDING_FINISHED:
			ESP_LOGI(TAG, "MUBBY_STATE_RECORDING_FINISHED");
			push_state(MUBBY_STATE_PLAYING);
			break;
			
		case MUBBY_STATE_PLAYING:
			ESP_LOGI(TAG, "MUBBY_STATE_PLAYING");
			xEventGroupSetBits(s_player_event_group, PLAYER_START);
			break;
			
		case MUBBY_STATE_PLAYING_FINISHED:
			ESP_LOGI(TAG, "MUBBY_STATE_PLAYING_FINISHED");
			close(s_server_sockfd);
			s_server_sockfd = -1;
			push_state(s_continue_chat ? MUBBY_STATE_CONNECTING : MUBBY_STATE_STANDBY);
			break;
			
		case MUBBY_STATE_SHUTDOWN:
			ESP_LOGI(TAG, "MUBBY_STATE_SHUTDOWN");
			break;
			
		default:
			ESP_LOGE(TAG, "Invalid status code received - %d\n", s_mubby_state);
		}		
	}
}
#endif

static void mqtt_start(void)
{
	const esp_mqtt_client_config_t mqtt_cfg = {
		.uri = "mqtt://"CONFIG_SERVER_HOST,
		.event_handle = mqtt_event_handler,
	};
	
	s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(s_mqtt_client);
}

void app_main(void)
{
	const char *TAG = "MUBBY";
	BaseType_t xReturned;
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    ESP_LOGI(TAG, "[APP] Startup...");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    
    s_board_handle = audio_board_init();
    
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PHERIPH_SET_CONFIG();
    g_periph_set = esp_periph_set_init(&periph_cfg);
    
    periph_button_cfg_t btn_cfg = {
		.gpio_mask = GPIO_SEL_36 | GPIO_SEL_39,
	};
	esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);	
	esp_periph_start(g_periph_set, button_handle);

	xReturned = xTaskCreate(vEventTask, "event_monitor", 2048, NULL, tskIDLE_PRIORITY + 2, NULL);
	configASSERT(xReturned == pdPASS);
	
	periph_sdcard_cfg_t sdcard_cfg = {
		.root = "/sdcard",
		.card_detect_pin = get_sdcard_intr_gpio(),
	};
	esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
	esp_periph_start(g_periph_set, sdcard_handle);
	
	ESP_LOGI(TAG, "Mounting SD card...");

	while (!periph_sdcard_is_mounted(sdcard_handle)) {
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
	
	ESP_LOGI(TAG, "Starting Wi-Fi...");

	/* start the HTTP Server task */
	xTaskCreateStatic(http_server, "http_server", sizeof(http_server_stack), NULL, tskIDLE_PRIORITY, http_server_stack, &http_server_task_buffer);

	/* start the wifi manager task */
	xTaskCreateStatic(wifi_manager, "wifi_manager", sizeof(wifi_manager_stack), NULL, tskIDLE_PRIORITY, wifi_manager_stack, &wifi_manager_task_buffer);

	mqtt_start();
	
	s_pending_states = xQueueCreate(10, sizeof(int));
	configASSERT(s_pending_states);
	
	s_recorder_event_group = xEventGroupCreate();
	configASSERT(s_recorder_event_group);
	
	s_player_event_group = xEventGroupCreate();
	configASSERT(s_player_event_group);
	
	xReturned = xTaskCreate(vRecorderTask, "recorder", 4096, NULL, tskIDLE_PRIORITY + 1, &xRecorderTask);
	configASSERT(xReturned == pdPASS);
	
	xReturned = xTaskCreate(vPlayerTask, "player", 4096, NULL, tskIDLE_PRIORITY + 1, &xPlayerTask);
	configASSERT(xReturned == pdPASS);
	
	push_state(MUBBY_STATE_STANDBY);
	
	for (;;) {
		xQueueReceive(s_pending_states, (void *)&s_mubby_state, portMAX_DELAY);
		switch (s_mubby_state) {
		case MUBBY_STATE_RESET:
			ESP_LOGI(TAG, "MUBBY_STATE_RESET");
			if (s_server_sockfd != -1) {
				close(s_server_sockfd);
				s_server_sockfd = -1;
			}
			push_state(MUBBY_STATE_STANDBY);
			break;
		
		case MUBBY_STATE_STANDBY:
			ESP_LOGI(TAG, "MUBBY_STATE_STANDBY");
			break;
			
		case MUBBY_STATE_CONNECTING:
			if (s_server_sockfd == -1) {
				struct sockaddr_in server_addr;
				s_server_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				configASSERT(s_server_sockfd > 0);
				memset(&server_addr, 0, sizeof(server_addr));
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = htons(CONFIG_SERVER_PORT);
				server_addr.sin_addr.s_addr = inet_addr(CONFIG_SERVER_HOST);
				ESP_LOGI(TAG, "Connecting to server...");
				if (connect(s_server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
					ESP_LOGE(TAG, "Failed to connect server");
					push_state(MUBBY_STATE_RESET);
					continue;
				}
				ESP_LOGI(TAG, "Connection established");
				push_state(MUBBY_STATE_RECORDING);
			} else {
				ESP_LOGE(TAG, "Connection has already established");
				push_state(MUBBY_STATE_RESET);
			}
			break;
			
		case MUBBY_STATE_RECORDING:
			ESP_LOGI(TAG, "MUBBY_STATE_RECORDING");
			xEventGroupSetBits(s_recorder_event_group, RECORDER_START);
			break;
			
		case MUBBY_STATE_STOP_RECORDING:
			ESP_LOGI(TAG, "MUBBY_STATE_STOP_RECORDING");
			xEventGroupSetBits(s_recorder_event_group, RECORDER_STOP);
			break;
			
		case MUBBY_STATE_RECORDING_FINISHED:
			ESP_LOGI(TAG, "MUBBY_STATE_RECORDING_FINISHED");
			push_state(MUBBY_STATE_PLAYING);
			break;
			
		case MUBBY_STATE_PLAYING:
			ESP_LOGI(TAG, "MUBBY_STATE_PLAYING");
			xEventGroupSetBits(s_player_event_group, PLAYER_START);
			break;
			
		case MUBBY_STATE_PLAYING_FINISHED:
			ESP_LOGI(TAG, "MUBBY_STATE_PLAYING_FINISHED");
			close(s_server_sockfd);
			s_server_sockfd = -1;
			push_state(s_continue_chat ? MUBBY_STATE_CONNECTING : MUBBY_STATE_STANDBY);
			break;
			
		case MUBBY_STATE_SHUTDOWN:
			ESP_LOGI(TAG, "MUBBY_STATE_SHUTDOWN");
			break;
			
		default:
			ESP_LOGE(TAG, "Invalid status code received - %d\n", s_mubby_state);
		}		
	}

}

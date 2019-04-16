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

#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "periph_button.h"
#include "board.h"

#include "http_server.h"
#include "wifi_manager.h"

#include "mubby.h"
#include "player.h"
#include "recorder.h"

static const char *TAG = "MUBBY";
static int s_player_volume = -1;

audio_board_handle_t g_board_handle = NULL;

static inline void push_state(app_context_handle_t ctx, mubby_state_t state)
{
	xQueueSend(ctx->msg_queue, (void *)&state, portMAX_DELAY);
}

static inline char *get_macaddr(app_context_handle_t app_ctx)
{
	char *macbuf = malloc(18);
	
	snprintf(macbuf, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		app_ctx->macaddr[0], app_ctx->macaddr[1], app_ctx->macaddr[2], 
		app_ctx->macaddr[3], app_ctx->macaddr[4], app_ctx->macaddr[5]);
		
	return macbuf;
}

static esp_err_t msg_parser(app_context_handle_t ctx, esp_mqtt_client_handle_t client, char *msg)
{
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
			ctx->cnt_chat = true;
		} else {
			ctx->cnt_chat = false;
		}
		
		{
			char topic[32], *macaddr;
			macaddr = get_macaddr(ctx);
			snprintf(topic, sizeof(topic), "mubby/server/%s", macaddr);
			free(macaddr);
			esp_mqtt_client_publish(client, topic, "{\"state\": \"ok\"}", 0, 1, 0);
		}
		ctx->stream->write(ctx->stream, (char []){'e', 'n', 'd'}, 3);
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
			if (!strcmp(act->valuestring, "up")) {
				s_player_volume += 10;
				if (s_player_volume > 100) {
					s_player_volume = 100;
				}
			} else if (!strcmp(act->valuestring, "down")) {
				s_player_volume -= 10;
				if (s_player_volume < 0) {
					s_player_volume = 0;
				}
			} else {
				ESP_LOGE(TAG, "Invalid action '%s'", act->valuestring);
				ret = ESP_ERR_INVALID_ARG;
				goto errout;
			}
			audio_hal_set_volume(g_board_handle->audio_hal, s_player_volume);			
		} else if (!strcmp(part->valuestring, "stt")) {
			if (!strcmp(act->valuestring, "end")) {
				ESP_ERROR_CHECK(recorder_stop(ctx->ar));
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
	app_context_handle_t ctx = (app_context_handle_t)event->user_context;
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	
	switch (event->event_id) {
	case MQTT_EVENT_CONNECTED:
		{
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			char topic[32], *macaddr;
			macaddr = get_macaddr(ctx);
			snprintf(topic, sizeof(topic), "mubby/client/%s", macaddr);
			free(macaddr);
			msg_id = esp_mqtt_client_subscribe(client, topic, 0);
			ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		}
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
		msg_parser(ctx, client, event->data);
		break;
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		break;
	default:
		break;
	}
	
	return ESP_OK;
}

static esp_err_t mqtt_start(app_context_handle_t ctx)
{
	const esp_mqtt_client_config_t mqtt_cfg = {
#ifdef CONFIG_ENABLE_SECURITY_PROTO
		.uri = "mqtts://"CONFIG_SERVER_HOST":8889",
		.cert_pem = (const char *)cert_pem_start,
		.client_cert_pem = (const char *)cert_pem_start,
		.client_key_pem = (const char *)privkey_pem_start,
#else
		.uri = "mqtt://"CONFIG_SERVER_HOST":8889",
#endif
		.event_handle = mqtt_event_handler,
		.user_context = ctx,
		
	};
	
	esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	return esp_mqtt_client_start(mqtt_client);
}

static bool mubby_auth(app_context_handle_t app_ctx)
{
	tcp_stream_handle_t stream = app_ctx->stream;
	char macbuf[18] = {0};
	char resp[8] = {0};
	
	snprintf(macbuf, sizeof(macbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
		app_ctx->macaddr[0], app_ctx->macaddr[1], app_ctx->macaddr[2], 
		app_ctx->macaddr[3], app_ctx->macaddr[4], app_ctx->macaddr[5]);
	
	if (stream->write(stream, macbuf, sizeof(macbuf)) < 0) {
		ESP_LOGE(TAG, "failed to send auth message");
		return false;
	}
	
	if (stream->read(stream, resp, sizeof(resp)) < 0) {
		ESP_LOGE(TAG, "failed to get response from server");
		return false;
	}
	
	if (!strncmp(resp, "accept", sizeof(resp))) {
		return true;
	}

	return false;	
}

static void event_monitor_task(void *pvParameters)
{
	app_context_handle_t ctx = (app_context_handle_t)pvParameters;
	audio_event_iface_handle_t evt = ctx->evt;
	
	bool pushed = false;
	
	ESP_LOGI(TAG, "EventMonitor created");
	
	for (;;) {
		audio_event_iface_msg_t msg;
		esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
		
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Event interface error: %d", ret);
			continue;
		}
		
		switch (msg.source_type) {
		case PERIPH_ID_BUTTON:
			if ((int)msg.data == GPIO_NUM_36) {	
				if (msg.cmd == PERIPH_BUTTON_PRESSED) {
					pushed = true;
				} else if (pushed && (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE)) {
					pushed = false;
					if (ctx->cur_state == MUBBY_STATE_STANDBY) {
						push_state(ctx, MUBBY_STATE_CONNECTING);
					}				
				}
			} else if ((int)msg.data == GPIO_NUM_39) {
				if (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE) {
					printf("home pushed\n");
					if (ctx->cur_state == MUBBY_STATE_PLAYING) {
						printf("stopping player\n");
						ESP_ERROR_CHECK(player_stop(ctx->ap));
					} else if (ctx->cur_state == MUBBY_STATE_RECORDING) {
						printf("stopping recorder\n");
						ESP_ERROR_CHECK(recorder_stop(ctx->ar));
						ctx->stream->write(ctx->stream, (char []){'b', 'r', 'k'}, 3);
					}
				}
			}
			break;
			
		case MUBBY_ID_PLAYER:
			if ((int)msg.data == PLAYER_STATE_STARTED) {
				push_state(ctx, MUBBY_STATE_PLAYING);
			} else if ((int)msg.data == PLAYER_STATE_FINISHED) {
				push_state(ctx, MUBBY_STATE_PLAYING_FINISHED);
			} else {
				ESP_LOGE(TAG, "Error occurred at player");
				push_state(ctx, MUBBY_STATE_RESET);
			}
			break;
			
		case MUBBY_ID_RECORDER:
			if ((int)msg.data == RECORDER_STATE_STARTED) {
				push_state(ctx, MUBBY_STATE_RECORDING);
			} else if ((int)msg.data == RECORDER_STATE_FINISHED) {
				push_state(ctx, MUBBY_STATE_RECORDING_FINISHED);
			} else {
				ESP_LOGE(TAG, "Error occurred at recorder");
				push_state(ctx, MUBBY_STATE_RESET);
			}
			break;
			
		case MUBBY_ID_WIFIMGR:
			if ((int)msg.data == WIFI_MANAGER_STATE_CONNECTED) {
				ESP_ERROR_CHECK(mqtt_start(ctx));
			} else if ((int)msg.data == WIFI_MANAGER_STATE_DISCONNECTED) {
				ESP_LOGW(TAG, "STA failed to attach AP");
			}
			break;
			
		default:
			ESP_LOGW(TAG, "Unknown event");
			break;
		}
	}
}

static void core_task(void *pvParameters)
{
	app_context_handle_t ctx = (app_context_handle_t)pvParameters;
	
	for (;;) {
		xQueueReceive(ctx->msg_queue, (void *)&ctx->cur_state, portMAX_DELAY);
		switch (ctx->cur_state) {
		case MUBBY_STATE_RESET:
			ESP_LOGE(TAG, "Reseting...");
			ctx->stream->close(ctx->stream);
			push_state(ctx, MUBBY_STATE_STANDBY);
			break;
			
		case MUBBY_STATE_STANDBY:
			ESP_LOGI(TAG, "Mubby is ready. Press REC key to start recording");
			break;
		
		case MUBBY_STATE_CONNECTING:
			ESP_LOGI(TAG, "Connecting to server");
			if (!ctx->stream->open(ctx->stream, CONFIG_SERVER_HOST, CONFIG_SERVER_PORT)) {
				push_state(ctx, MUBBY_STATE_RESET);
				continue;
			} else {
				struct timeval timeout = {2, 0};
				tcp_stream_set_timeout(ctx->stream, &timeout);
				if (!mubby_auth(ctx)) {
					push_state(ctx, MUBBY_STATE_RESET);
					continue;
				}
			}
			if (ctx->stream->write(ctx->stream, (char []){'r', 'e', 'c'}, 3) != 3) {
				ESP_LOGE(TAG, "Failed to send 'rec' to server");
				push_state(ctx, MUBBY_STATE_RESET);
			}
			ESP_ERROR_CHECK(recorder_start(ctx->ar));
			break;
			
		case MUBBY_STATE_RECORDING:
			ESP_LOGI(TAG, "Recording...");
			break;
			
		case MUBBY_STATE_RECORDING_FINISHED:
			ESP_LOGI(TAG, "Recording finished");
			ESP_ERROR_CHECK(player_start(ctx->ap));
			break;
			
		case MUBBY_STATE_PLAYING:
			ESP_LOGI(TAG, "Playing...");
			break;
			
		case MUBBY_STATE_PLAYING_FINISHED:
			ESP_LOGI(TAG, "Playing finished");
			ctx->stream->close(ctx->stream);
			if (ctx->cnt_chat) {
				push_state(ctx, MUBBY_STATE_CONNECTING);
			} else {
				push_state(ctx, MUBBY_STATE_STANDBY);
			}
			break;
			
		default:
			break;
		} 
	}
}

void app_main(void)
{
	BaseType_t xReturned; 
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    g_board_handle = audio_board_init();
    audio_hal_ctrl_codec(g_board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_get_volume(g_board_handle->audio_hal, &s_player_volume);
    
    ESP_LOGI(TAG, "Volume: %d", s_player_volume);
    
    
    app_context_handle_t app_ctx;
    
    app_ctx = calloc(1, sizeof(app_context_t));
    mem_assert(app_ctx);
    
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	app_ctx->evt = audio_event_iface_init(&evt_cfg);
    mem_assert(app_ctx->evt);
    
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	esp_periph_set_handle_t periph_set = esp_periph_set_init(&periph_cfg);
    
    periph_button_cfg_t btn_cfg = {
		.gpio_mask = GPIO_SEL_36 | GPIO_SEL_39,
	};
	esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);	
	esp_periph_start(periph_set, button_handle);
	ESP_ERROR_CHECK(audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set), app_ctx->evt));

	app_ctx->stream = tcp_stream_create();
	mem_assert(app_ctx->stream);
	
	app_ctx->ap = player_create();
	mem_assert(app_ctx->ap);
	ESP_ERROR_CHECK(player_set_event_listener(app_ctx->ap, app_ctx->evt));
	ESP_ERROR_CHECK(player_set_tcp_stream(app_ctx->ap, app_ctx->stream));

	app_ctx->ar = recorder_create();
	ESP_ERROR_CHECK(recorder_set_event_listener(app_ctx->ar, app_ctx->evt));
	ESP_ERROR_CHECK(recorder_set_tcp_stream(app_ctx->ar, app_ctx->stream));
	 
	app_ctx->msg_queue = xQueueCreate(10, sizeof(int));
	mem_assert(app_ctx->msg_queue);
	
	ESP_LOGI(TAG, "Starting Wi-Fi...");

	/* start the HTTP Server task */
	ESP_ERROR_CHECK(http_server_start());

	/* start the wifi manager task */
	ESP_ERROR_CHECK(wifi_manager_start(app_ctx, app_ctx->evt));
	
	xReturned = xTaskCreate(event_monitor_task, "event_monitor", 2048, (void *)app_ctx, tskIDLE_PRIORITY + 2, NULL);
	configASSERT(xReturned == pdPASS);
	
	xReturned = xTaskCreate(core_task, "core_task", 2048, (void *)app_ctx, tskIDLE_PRIORITY, NULL);
	configASSERT(xReturned == pdPASS);
	
	ESP_LOGI(TAG, "[APP] Startup...");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
	
	push_state(app_ctx, MUBBY_STATE_STANDBY);
}

/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "sdkconfig.h"
#include "esp_adc_cal.h"
#include "esp_system.h"

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

#define V_REF   1100
#define ADC1_TEST_CHANNEL (ADC1_CHANNEL_6)      //GPIO 34

#define BLINK_GPIO CONFIG_BLINK_GPIO

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "192.168.1.1"
#define WEB_PORT 80
#define WEB_URL "http://192.168.1.1:80/"

static const char *TAG = "example";

int flag = 0;

static const char *REQUEST = "POST /osc/commands/execute HTTP/1.1\r\n"
    "Host: "WEB_SERVER"\r\n"
    "Content-Length: 31\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n"
    "{\"name\": \"camera.startSession\"}\r\n";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r, j, l;
    char recv_buf[64], session_id[9], send_buf[1024], data[256];

    gpio_pad_select_gpio(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while(1) {
	    /* Wait for the callback to set the CONNECTED_BIT in the
	       event group.
	     */
	    if(flag == 1){
		    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
				    false, true, portMAX_DELAY);
		    ESP_LOGI(TAG, "Connected to AP");

		    int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

		    if(err != 0 || res == NULL) {
			    ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
			    vTaskDelay(1000 / portTICK_PERIOD_MS);
			    continue;
		    }

		    /* Code to print the resolved IP.

Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
		    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
		    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

		    s = socket(res->ai_family, res->ai_socktype, 0);
		    if(s < 0) {
			    ESP_LOGE(TAG, "... Failed to allocate socket.");
			    freeaddrinfo(res);
			    vTaskDelay(1000 / portTICK_PERIOD_MS);
			    continue;
		    }
		    ESP_LOGI(TAG, "... allocated socket");

		    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
			    ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
			    close(s);
			    freeaddrinfo(res);
			    vTaskDelay(4000 / portTICK_PERIOD_MS);
			    continue;
		    }

		    ESP_LOGI(TAG, "... connected");
		    freeaddrinfo(res);

		    if (write(s, REQUEST, strlen(REQUEST)) < 0) {
			    ESP_LOGE(TAG, "... socket send failed");
			    close(s);
			    vTaskDelay(4000 / portTICK_PERIOD_MS);
			    continue;
		    }
		    ESP_LOGI(TAG, "... socket send success");

		    struct timeval receiving_timeout;
		    receiving_timeout.tv_sec = 5;
		    receiving_timeout.tv_usec = 0;
		    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
					    sizeof(receiving_timeout)) < 0) {
			    ESP_LOGE(TAG, "... failed to set socket receiving timeout");
			    close(s);
			    vTaskDelay(4000 / portTICK_PERIOD_MS);
			    continue;
		    }
		    ESP_LOGI(TAG, "... set socket receiving timeout success");

		    /* Read HTTP response */
		    j = 0;
		    l = 0;
		    bzero(session_id, sizeof(session_id));
		    do {
			    bzero(recv_buf, sizeof(recv_buf));
			    r = read(s, recv_buf, sizeof(recv_buf)-1);
			    for(int i = 0; i < r; i++) {
				    //putchar(recv_buf[i]);
				    if(j > 69 && j < 78){
					    session_id[l++] = recv_buf[i];
				    }
				    if(recv_buf[i] == '{' || j > 0){
					    j++;
				    }
			    }
		    } while(r > 0);
		    //puts(session_id);


		    bzero(send_buf, sizeof(send_buf));
		    bzero(data, sizeof(data));
		    sprintf(data, "{\"name\": \"camera.takePicture\", \"parameters\": {\"sessionId\": \"%s\"}}",session_id);
		    sprintf(send_buf,"POST /osc/commands/execute HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s\r\n",strlen(data),data);

		    if (write(s, send_buf, strlen(send_buf)) < 0) {
			    ESP_LOGE(TAG, "... socket send failed");
			    close(s);
			    vTaskDelay(4000 / portTICK_PERIOD_MS);
			    continue;
		    }
                    flag = 0;
                    printf("take a picture\n");

        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

		    ESP_LOGI(TAG, "... socket send success");

		    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
		    close(s);
		    for(int countdown = 10; countdown >= 0; countdown--) {
			    ESP_LOGI(TAG, "%d... ", countdown);
			    vTaskDelay(1000 / portTICK_PERIOD_MS);
		    }
		    ESP_LOGI(TAG, "Starting again!");
	    }
            vTaskDelay(1);
    }
}

static void sound_check(void *pvParameters)
{
#ifndef V_REF_TO_GPIO
	//Init ADC and Characteristics
	esp_adc_cal_characteristics_t characteristics;
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC1_TEST_CHANNEL, ADC_ATTEN_DB_0);
	esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_12, &characteristics);
	uint32_t voltage;
	while(1){
		voltage = adc1_to_voltage(ADC1_TEST_CHANNEL, &characteristics);
		printf("%d mV\n",voltage);
		if(voltage > 1000 && flag == 0){
		        flag = 1;
			vTaskDelay(pdMS_TO_TICKS(4500));
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}
#else
	//Get v_ref
	esp_err_t status;
	status = adc2_vref_to_gpio(GPIO_NUM_25);
	if (status == ESP_OK){
		printf("v_ref routed to GPIO\n");
	}else{
		printf("failed to route v_ref\n");
	}
	fflush(stdout);
#endif
}

void app_main()
{
	ESP_ERROR_CHECK( nvs_flash_init() );
	initialise_wifi();
	xTaskCreatePinnedToCore(&http_get_task, "http_get_task", 4096, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&sound_check, "sound_check", 4096, NULL, 5, NULL, 1);

	//xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);
}

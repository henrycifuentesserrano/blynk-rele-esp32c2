#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID         CONFIG_WIFI_SSID
#define WIFI_PASS         CONFIG_WIFI_PASSWORD
#define BLYNK_AUTH        CONFIG_BLYNK_AUTH_TOKEN
#define BLYNK_TEMPLATE_ID CONFIG_BLYNK_TEMPLATE_ID

#define PIN_RELE     GPIO_NUM_5
#define LED_RELE     GPIO_NUM_10
#define LED_WIFI     GPIO_NUM_18

static const char *TAG = "blynk-rele";

// Estados del sistema
typedef enum {
    ESTADO_INICIANDO,
    ESTADO_CONECTANDO_BLYNK,
    ESTADO_OPERANDO,
    ESTADO_RECONECTANDO
} estado_t;

static estado_t estado_actual = ESTADO_INICIANDO;
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static esp_mqtt_client_handle_t mqtt_client = NULL;

// Tarea que maneja el parpadeo del LED WiFi segun el estado
static void tarea_led(void *pvParameters)
{
    while (1) {
        switch (estado_actual) {
            case ESTADO_INICIANDO:
                gpio_set_level(LED_WIFI, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(LED_WIFI, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case ESTADO_CONECTANDO_BLYNK:
                gpio_set_level(LED_WIFI, 1);
                vTaskDelay(pdMS_TO_TICKS(250));
                gpio_set_level(LED_WIFI, 0);
                vTaskDelay(pdMS_TO_TICKS(250));
                break;

            case ESTADO_OPERANDO:
                gpio_set_level(LED_WIFI, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case ESTADO_RECONECTANDO:
                gpio_set_level(LED_WIFI, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(LED_WIFI, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }
    }
}

static void set_rele(int state)
{
    gpio_set_level(PIN_RELE, state);
    gpio_set_level(LED_RELE, state);
    ESP_LOGI(TAG, "Rele %s", state ? "ON" : "OFF");
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi desconectado, reconectando...");
        estado_actual = ESTADO_RECONECTANDO;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        estado_actual = ESTADO_CONECTANDO_BLYNK;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado a Blynk MQTT!");
            estado_actual = ESTADO_OPERANDO;
            esp_mqtt_client_subscribe(mqtt_client, "downlink/#", 1);
            esp_mqtt_client_publish(mqtt_client, "info/mcu",
                "{\"tmpl\":\"" BLYNK_TEMPLATE_ID "\",\"ver\":\"0.1.0\",\"build\":\"Jan 1 2025\"}",
                0, 0, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Desconectado de Blynk MQTT");
            estado_actual = ESTADO_RECONECTANDO;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);

            if (event->topic_len > 0 && strncmp(event->topic, "downlink/redirect", 17) == 0) {
                ESP_LOGW(TAG, "Blynk redirige a: %.*s", event->data_len, event->data);
            }

            if (event->topic_len > 0 && strncmp(event->topic, "downlink/ds/Rele", 16) == 0) {
                char data[16] = {0};
                memcpy(data, event->data, event->data_len < 15 ? event->data_len : 15);
                if (strcmp(data, "1") == 0) {
                    set_rele(1);
                } else if (strcmp(data, "0") == 0) {
                    set_rele(0);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error MQTT");
            estado_actual = ESTADO_RECONECTANDO;
            break;

        default:
            break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = "mqtts://blynk.cloud:8883",
            },
            .verification = {
                .skip_cert_common_name_check = true,
                .use_global_ca_store = false,
                .crt_bundle_attach = esp_crt_bundle_attach,
            },
        },
        .credentials = {
            .username = "device",
            .client_id = BLYNK_AUTH,
            .authentication = {
                .password = BLYNK_AUTH,
            },
        },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void)
{
    nvs_flash_init();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_RELE) | (1ULL << LED_RELE) | (1ULL << LED_WIFI),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_RELE, 0);
    gpio_set_level(LED_RELE, 0);
    gpio_set_level(LED_WIFI, 0);

    // Iniciar tarea de LEDs
    xTaskCreate(tarea_led, "tarea_led", 2048, NULL, 5, NULL);

    estado_actual = ESTADO_INICIANDO;
    wifi_init();
    mqtt_init();
}
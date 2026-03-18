idf_component_register(SRCS "alarma_main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_gpio esp_netif esp_event esp_timer mqtt mbedtls esp-tls esp_http_server
                    INCLUDE_DIRS ".")

target_add_binary_data(__idf_main "../certs/certificate.pem.crt" TEXT)
target_add_binary_data(__idf_main "../certs/private.pem.key" TEXT)
target_add_binary_data(__idf_main "../certs/root_ca.pem" TEXT)
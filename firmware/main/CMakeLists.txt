idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES esp_driver_i2c esp_netif esp_timer esp_wifi lwip nvs_flash
                       INCLUDE_DIRS "")

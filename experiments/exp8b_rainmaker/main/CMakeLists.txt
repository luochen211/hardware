idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_driver_gpio esp_driver_spi esp_adc driver rmaker_app_network rmaker_app_insights)

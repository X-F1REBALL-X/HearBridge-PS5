idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES usb_in nvs_flash esp_ringbuf esp_timer)

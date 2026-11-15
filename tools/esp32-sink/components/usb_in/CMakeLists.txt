idf_component_register(SRCS "usb_in.c" "usb_descriptors.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "tusb"
                       REQUIRES esp_ringbuf
                       PRIV_REQUIRES espressif__tinyusb usb esp_timer
                       WHOLE_ARCHIVE)

# TinyUSB pulls "tusb_config.h" from its includer; give the tinyusb lib our config.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tusb")

# usb_in

USB Audio Class 1.0 device (speaker) for the ESP32-S3, built on TinyUSB native UAC1. It receives isochronous PCM from the USB host and pushes it into a FreeRTOS ring buffer. Default format is 48 kHz 16-bit stereo.

The public API (`usb_in_init`, `usb_in_start`, `usb_in_read`, `usb_in_get_ringbuf`, `usb_in_get_stats`) is kept compatible with netham45/esp32-usb-in so it can drop into that project.

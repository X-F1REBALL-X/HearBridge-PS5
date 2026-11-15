# esp32-sink: ESP32-S3 "PS5 Audio Sink"

Small ESP32-S3 firmware that lets HearBridge grab PS5 audio without a real powered USB headset plugged in.

## Why
HearBridge's USB audio capture rides on the PS5's `uaudio` playback ring, and the console only fills that ring while a powered USB audio output device is selected. Normally that means plugging in a real USB headset or dongle. This firmware turns a cheap ESP32-S3 DevKitC-1 into that device. The PS5 sees it as a generic USB speaker and renders its whole mix (Tempest 3D folded down to binaural stereo) into the `uaudio` buffer, and HearBridge streams it out to whatever Bluetooth headphones you pair.

The ESP just reads the incoming PCM and throws it away. It has no DAC output and no Bluetooth of its own. All it does is keep the PS5 USB audio path alive.

## How it shows up
- USB Audio Class 1.0 (bcdADC 0x0100, because the PS5 ignores UAC2 devices)
- 48 kHz / 16-bit / stereo PCM, adaptive isochronous OUT, Full-Speed, no feedback endpoint
- VID/PID 0x303A/0x4011, product string "PS5 Audio Sink"
- Listed on the PS5 as "USB Headphones (PS5 Audio Sink)"

Once a second it logs over UART0:

    UAC: connected=1 streaming=1 rate=48000 bytes/s=192000 peak=12345 mute=0 vol=0/256dB dropped=0

## Build
ESP-IDF v5.4.1 with espressif/tinyusb 0.21.0~6 (TinyUSB native UAC1).

    idf.py set-target esp32s3
    idf.py build

Target board: ESP32-S3 DevKitC-1 N16R8 (16 MB flash, 8 MB octal PSRAM).

## Flash (no build needed)
A prebuilt merged image sits in `firmware/`. Flash it to the UART port (the CP2102 connector, not the native USB-OTG one):

    esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 firmware/esp32-ps5-uac-sink-merged.bin

Or flash from a browser with [esptool-js](https://espressif.github.io/esptool-js/) at offset 0x0. Monitor at 115200 baud on the same UART port.

Then plug the USB-OTG port into the PS5. In the PS5 Settings > Sound > Audio Output, pick "USB Headphones (PS5 Audio Sink)" as the output, and in HearBridge set the capture source to USB audio.

Checksums are in `firmware/SHA256SUMS`.

# ESP32S3 ILI9341 320x240 LCD demo project
This project uses a generic ESP32S3 board (in fact, it's a ESP32 NodeMCU modified so it can use a ESP32S3) to interface an ILI9341 320x240 LCD running on SPI at 40MHz.

Uses the `SPI2_HOST` and its non-muxed pins to achieve a theoretically 80MHz clock. 80MHz seems to be too much for my crude connections, or the ILI9341 itself; while 40MHz runs fine.

## Hardware setup
The LCD needs to be wired up to the ESP32S3 as follows:
| ESP32S3 pin    | LCD pin     | Remarks                 |
|----------------|-------------|-------------------------|
| IO13 (pin #21) | SDI SPI/SDA | MOSI                    |
| IO12 (pin #20) | SDO SPI     | MISO (not used on Doom) |
| IO14 (pin #22) | DC/SCL SPI  | CLK                     |
| IO15 (pin #8)  | CS/SPI CS   | /CS                     |
| IO16 (pin #9)  | WR/SPI D/C  | DC (data/command)       |
| IO17 (pin #10) | RESET       | /RESET                  |
| IO18 (pin #11) | .           | Backlight enable, usually connected to a transistor to turn on the backlight |

The DOOM example uses audio output. However, since the ESP32S3 doesn't have a DAC, I've used the old trick of using the I2S peripheral in PDM mode as a crude DAC. You need to filter the output out and amplify it, but believe me that plugging a speaker directly to the data pin just "works" (I know it's really bad... but hey!). The output is the `IO38` (pin #31).

## How to use
This requires platformio. If you want to build the DOOM demo, then uncomment line 2 on `app_main.c`. Build and upload the project through platformio. Get DOOM2.WAD from https://archive.org/details/DOOM2IWADFILE; and prboom-plus.wad from https://github.com/coelckers/prboom-plus/releases/download/v2.6.66/prboom-plus-2666-ucrt64.zip.
In theory, you should be able to use DOOM.WAD (Doom1), but I didn't try that.
Modify the `upload.sh` script to match the port used in your board, then run the `upload.sh` script to upload `DOOM2.WAD` and `prboom-plus.wad`.

If you want to use the LVGL demo, leave line 2 commented on `app_main.c`, build and upload the project through platformio.

## Sources in use
* lvgl 8.37.7 @ https://github.com/lvgl/lvgl
* lvgl-esp32-drivers lvgl_8.3.7_idf_5.2 @ https://github.com/hiruna/lvgl_esp32_drivers
* prboom and prboom-wad-tables @ https://github.com/espressif/esp32-doom/tree/esp32-s3-box
* prboom-esp32-compat (based off espressif's) @ https://github.com/arkadijs/esp32-doom (modified to use the ILI9341 back again)

## TODO
* Embed any kind of MIDI player and/or a MUS2IMF or MIDI2IMF to reuse the Espressif's IMF player.
* Enable USB host to use a keyboard and mouse.

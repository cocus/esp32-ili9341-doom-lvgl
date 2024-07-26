#!/bin/sh -xe
PORT="COM25"
esptool="python esptool.py --chip esp32s3 --port $PORT --baud 921600 --before default_reset --after hard_reset write_flash --flash_mode qio --flash_freq 80m --flash_size 16MB"
DOOMWADDIR=.
$esptool 0x120000 $DOOMWADDIR/doom2.wad
$esptool 0xFA0000 $DOOMWADDIR/prboom-plus.wad

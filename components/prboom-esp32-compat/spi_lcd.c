// Copyright 2016-2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

#include "sdkconfig.h"

#define PIN_NUM_MISO CONFIG_LV_DISP_SPI_MISO
#define PIN_NUM_MOSI CONFIG_LV_DISP_SPI_MOSI
#define PIN_NUM_CLK CONFIG_LV_DISP_SPI_CLK

#define PIN_NUM_CS CONFIG_LV_DISP_SPI_CS
#define PIN_NUM_DC CONFIG_LV_DISP_PIN_DC
#define PIN_NUM_RST CONFIG_LV_DISP_PIN_RST
#define PIN_NUM_BCKL CONFIG_LV_DISP_PIN_BCKL

#if (CONFIG_LV_BACKLIGHT_ACTIVE_LVL==0)
#define HW_INV_BL
#endif

// You want this, especially at higher framerates. The 2nd buffer is allocated in iram anyway, so isn't really in the way.
#define DOUBLE_BUFFER

/*
 The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; // No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} ili_init_cmd_t;

// ili9341
static const ili_init_cmd_t ili_init_cmds[] = {

    {0xCF, {0x00, 0x83, 0X30}, 3},
    {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0x28}, 1},
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
    {0x2C, {0}, 0},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xff},
};

static spi_device_handle_t spi;

// Send a command to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
void ili_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));           // Zero out the transaction
    t.length = 8;                       // Command is 8 bits
    t.tx_buffer = &cmd;                 // The data is the cmd itself
    t.user = (void *)0;                 // D/C needs to be set to 0
    ret = spi_device_transmit(spi, &t); // Transmit!
    assert(ret == ESP_OK);              // Should have had no issues.
}

// Send data to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
void ili_data(spi_device_handle_t spi, const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len == 0)
        return;                         // no need to send anything
    memset(&t, 0, sizeof(t));           // Zero out the transaction
    t.length = len * 8;                 // Len is in bytes, transaction length is in bits.
    t.tx_buffer = data;                 // Data
    t.user = (void *)1;                 // D/C needs to be set to 1
    ret = spi_device_transmit(spi, &t); // Transmit!
    assert(ret == ESP_OK);              // Should have had no issues.
}

// This function is called (in irq context!) just before a transmission starts. It will
// set the D/C line to the value indicated in the user field.
void ili_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

// Initialize the display
void ili_init(spi_device_handle_t spi)
{
    /// Enable backlight We do this first as for some hardware this enables the LCD power as well.
#ifdef HW_INV_BL
    gpio_set_level(PIN_NUM_BCKL, 0);
#else
    gpio_set_level(PIN_NUM_BCKL, 1);
#endif

    int cmd = 0;
    // Initialize non-SPI GPIOs
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);

    // Reset the display
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Send all the commands
    while (ili_init_cmds[cmd].databytes != 0xff)
    {
        uint8_t dmdata[16];
        ili_cmd(spi, ili_init_cmds[cmd].cmd);
        // Need to copy from flash to DMA'able memory
        memcpy(dmdata, ili_init_cmds[cmd].data, 16);
        ili_data(spi, dmdata, ili_init_cmds[cmd].databytes & 0x1F);
        if (ili_init_cmds[cmd].databytes & 0x80)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        cmd++;
    }
}

static void send_header_start(spi_device_handle_t spi, int xpos, int ypos, int w, int h)
{
    esp_err_t ret;
    int x;
    // Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    // function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans[5];

    // In theory, it's better to initialize trans and data only once and hang on to the initialized
    // variables. We allocate them on the stack, so we need to re-init them each call.
    for (x = 0; x < 5; x++)
    {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x & 1) == 0)
        {
            // Even transfers are commands
            trans[x].length = 8;
            trans[x].user = (void *)0;
        }
        else
        {
            // Odd transfers are data
            trans[x].length = 8 * 4;
            trans[x].user = (void *)1;
        }
        trans[x].flags = SPI_TRANS_USE_TXDATA;
    }
    trans[0].tx_data[0] = 0x2A;                  // Column Address Set
    trans[1].tx_data[0] = xpos >> 8;             // Start Col High
    trans[1].tx_data[1] = xpos;                  // Start Col Low
    trans[1].tx_data[2] = (xpos + w - 1) >> 8;   // End Col High
    trans[1].tx_data[3] = (xpos + w - 1) & 0xff; // End Col Low
    trans[2].tx_data[0] = 0x2B;                  // Page address set
    trans[3].tx_data[0] = ypos >> 8;             // Start page high
    trans[3].tx_data[1] = ypos & 0xff;           // start page low
    trans[3].tx_data[2] = (ypos + h - 1) >> 8;   // end page high
    trans[3].tx_data[3] = (ypos + h - 1) & 0xff; // end page low
    trans[4].tx_data[0] = 0x2C;                  // memory write

    // Queue all transactions.
    for (x = 0; x < 5; x++)
    {
        ret = spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
        assert(ret == ESP_OK);
    }

    // When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    // mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
    // finish because we may as well spend the time calculating the next line. When that is done, we can call
    // send_line_finish, which will wait for the transfers to be done and check their status.
}

void send_header_cleanup(spi_device_handle_t spi)
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    // Wait for all 5 transactions to be done and get back the results.
    for (int x = 0; x < 5; x++)
    {
        ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
        assert(ret == ESP_OK);
        // We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
    }
}

#ifndef DOUBLE_BUFFER
volatile static uint16_t *currFbPtr = NULL;
#else
// Warning: This gets squeezed into IRAM.
static uint32_t *currFbPtr = NULL;
#endif
SemaphoreHandle_t dispSem = NULL;
SemaphoreHandle_t dispDoneSem = NULL;

#define NO_SIM_TRANS 5         // Amount of SPI transfers to queue in parallel
#define MEM_PER_TRANS 1024 * 3 // in 16-bit words

extern int16_t lcdpal[256];

void IRAM_ATTR displayTask(void *arg)
{
    int x, i;
    int idx = 0;
    int inProgress = 0;
    static uint16_t *dmamem[NO_SIM_TRANS];
    spi_transaction_t trans[NO_SIM_TRANS];
    spi_transaction_t *rtrans;

    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (MEM_PER_TRANS * 2) + 16};
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40000000,              // Clock out at 40 MHz. Yes, that's heavily overclocked.
        .mode = 0,                               // SPI mode 0
        .spics_io_num = PIN_NUM_CS,              // CS pin
        .queue_size = NO_SIM_TRANS,              // We want to be able to queue this many transfers
        .pre_cb = ili_spi_pre_transfer_callback, // Specify pre-transfer callback to handle D/C line
    };

    printf("*** Display task starting. ILI9341, speed = %d\n", devcfg.clock_speed_hz);

    // Initialize the SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);
    // Attach the LCD to the SPI bus
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    assert(ret == ESP_OK);
    // Initialize the LCD
    ili_init(spi);

    // We're going to do a fair few transfers in parallel. Set them all up.
    for (x = 0; x < NO_SIM_TRANS; x++)
    {
        dmamem[x] = heap_caps_malloc(MEM_PER_TRANS * 2, MALLOC_CAP_DMA);
        assert(dmamem[x]);
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        trans[x].length = MEM_PER_TRANS * 2;
        trans[x].user = (void *)1;
        trans[x].tx_buffer = &dmamem[x];
    }
    xSemaphoreGive(dispDoneSem);

    while (1)
    {
        xSemaphoreTake(dispSem, portMAX_DELAY);
//		printf("Display task: frame.\n");
#ifndef DOUBLE_BUFFER
        uint8_t *myData = (uint8_t *)currFbPtr;
#endif

        send_header_start(spi, 0, 0, 320, 240);
        send_header_cleanup(spi);
        for (x = 0; x < 320 * 240; x += MEM_PER_TRANS)
        {
#ifdef DOUBLE_BUFFER
            for (i = 0; i < MEM_PER_TRANS; i += 4)
            {
                uint32_t d = currFbPtr[(x + i) / 4];
                dmamem[idx][i + 0] = lcdpal[(d >> 0) & 0xff];
                dmamem[idx][i + 1] = lcdpal[(d >> 8) & 0xff];
                dmamem[idx][i + 2] = lcdpal[(d >> 16) & 0xff];
                dmamem[idx][i + 3] = lcdpal[(d >> 24) & 0xff];
            }
#else
            for (i = 0; i < MEM_PER_TRANS; i++)
            {
                dmamem[idx][i] = lcdpal[myData[i]];
            }
            myData += MEM_PER_TRANS;
#endif
            trans[idx].length = MEM_PER_TRANS * 16;
            trans[idx].user = (void *)1;
            trans[idx].tx_buffer = dmamem[idx];
            ret = spi_device_queue_trans(spi, &trans[idx], portMAX_DELAY);
            assert(ret == ESP_OK);

            idx++;
            if (idx >= NO_SIM_TRANS)
                idx = 0;

            if (inProgress == NO_SIM_TRANS - 1)
            {
                ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
                assert(ret == ESP_OK);
            }
            else
            {
                inProgress++;
            }
        }
#ifndef DOUBLE_BUFFER
        xSemaphoreGive(dispDoneSem);
#endif
        while (inProgress)
        {
            ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
            assert(ret == ESP_OK);
            inProgress--;
        }
    }
}

void spi_lcd_wait_finish()
{
#ifndef DOUBLE_BUFFER
    xSemaphoreTake(dispDoneSem, portMAX_DELAY);
#endif
}

void spi_lcd_send(uint16_t *scr)
{
#ifdef DOUBLE_BUFFER
    memcpy(currFbPtr, scr, 320 * 240);
    // Theoretically, also should double-buffer the lcdpal array... ahwell.
#else
    currFbPtr = scr;
#endif
    xSemaphoreGive(dispSem);
}

void spi_lcd_init()
{
    printf("spi_lcd_init()\n");
    dispSem = xSemaphoreCreateBinary();
    dispDoneSem = xSemaphoreCreateBinary();
#ifdef DOUBLE_BUFFER
    currFbPtr = heap_caps_malloc(320 * 240, /*MALLOC_CAP_32BIT*/ MALLOC_CAP_SPIRAM);
#endif
#if CONFIG_FREERTOS_UNICORE
    xTaskCreatePinnedToCore(&displayTask, "display", 6000, NULL, 6, NULL, 0);
#else
    xTaskCreatePinnedToCore(&displayTask, "display", 6000, NULL, 6, NULL, 1);
#endif
}

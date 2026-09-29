#ifndef WHISPLAY_DISPLAY_H
#define WHISPLAY_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

/* Panel: ST7789, 240x280, 4-wire SPI, on the PiSugar Whisplay HAT. */
#define WHISPLAY_WIDTH  240
#define WHISPLAY_HEIGHT 280

typedef struct {
    const char *spi_dev;   /* e.g. "/dev/spidev0.0" */
    uint32_t    spi_hz;    /* SPI clock in Hz */
    int         pin_rst;   /* BCM GPIO, LCD reset */
    int         pin_dc;    /* BCM GPIO, data/command */
    int         pin_bl;    /* BCM GPIO, backlight (LCDCTRL), active low */
    int         buf_lines; /* LVGL render buffer height in lines (partial render mode) */
} whisplay_config_t;

/* Defaults for a Pi Zero 2 W: spidev0.0, 100 MHz, RST=GPIO4, DC=GPIO27, BL=GPIO22, 70-line buffer */
whisplay_config_t whisplay_default_config(void);

/* Create the LVGL display. Call lv_init() first. Returns NULL on failure (reason on stderr).
 * The backlight stays off until whisplay_backlight(true). */
lv_display_t *whisplay_display_create(const whisplay_config_t *cfg);

void whisplay_backlight(bool on);

/* Turns the backlight off and releases SPI and GPIO. */
void whisplay_display_destroy(void);

#endif /* WHISPLAY_DISPLAY_H */

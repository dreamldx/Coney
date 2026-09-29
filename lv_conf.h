/* Minimal LVGL configuration for Coney (Pi Zero 2 W + Whisplay HAT).
 * Anything not set here takes LVGL's default from lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (256 * 1024U)

/* Roughly the panel refresh rate (about 60 Hz); the panel has no TE line to sync to */
#define LV_DEF_REFR_PERIOD 16

#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0

/* ST7789 driver, used through the generic MIPI LCD driver */
#define LV_USE_GENERIC_MIPI 1
#define LV_USE_ST7789 1

#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
/* Smallest built-in font with CJK glyphs */
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1

#endif /* LV_CONF_H */

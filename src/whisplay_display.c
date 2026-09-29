/* Whisplay LCD: LVGL's lv_st7789 driver on top of spidev, with DC/RST/backlight driven
 * through /dev/gpiomem. See docs/DISPLAY.md in the Coney project for why this path is used
 * instead of the kernel panel-mipi-dbi driver. */
#include "whisplay_display.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/spi/spidev.h>

static struct {
    volatile uint32_t *gpio;
    int spi_fd;
    int gpio_fd;
    size_t chunk;
    int pin_rst, pin_dc, pin_bl;
    uint8_t *buf;
    bool failed;
} g = { .spi_fd = -1, .gpio_fd = -1, .chunk = 4096 };

/* ---- GPIO (BCM283x registers through /dev/gpiomem) ---- */

static void gpio_output(int pin)
{
    volatile uint32_t *fsel = g.gpio + pin / 10;
    int shift = (pin % 10) * 3;
    *fsel = (*fsel & ~(7u << shift)) | (1u << shift);
}

static void gpio_write(int pin, int value)
{
    if (value) g.gpio[7] = 1u << pin;   /* GPSET0 */
    else       g.gpio[10] = 1u << pin;  /* GPCLR0 */
}

/* ---- SPI ---- */

static void spi_write(const uint8_t *p, size_t n)
{
    while (n) {
        size_t c = n < g.chunk ? n : g.chunk;
        struct spi_ioc_transfer tr = { .tx_buf = (unsigned long)p, .len = (uint32_t)c };
        if (ioctl(g.spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            if (!g.failed) perror("whisplay: spi write");
            g.failed = true;
            return;
        }
        p += c;
        n -= c;
    }
}

static void send_cmd(lv_display_t *d, const uint8_t *cmd, size_t cmd_size,
                     const uint8_t *param, size_t param_size)
{
    (void)d;
    gpio_write(g.pin_dc, 0);
    spi_write(cmd, cmd_size);
    if (param_size) {
        gpio_write(g.pin_dc, 1);
        spi_write(param, param_size);
    }
}

static void send_color(lv_display_t *d, const uint8_t *cmd, size_t cmd_size,
                       uint8_t *param, size_t param_size)
{
    gpio_write(g.pin_dc, 0);
    spi_write(cmd, cmd_size);
    gpio_write(g.pin_dc, 1);
    /* LVGL renders little-endian RGB565, the panel wants the high byte first */
    lv_draw_sw_rgb565_swap(param, param_size / 2);
    spi_write(param, param_size);
    lv_display_flush_ready(d);
}

/* ---- Whisplay panel tuning (from the vendor's runtime/whisplay.py) ---- */

static const struct { uint8_t cmd, n; uint8_t p[14]; } tuning[] = {
    { 0xB2, 5,  { 0x0C, 0x0C, 0x00, 0x33, 0x33 } },
    { 0xB7, 1,  { 0x35 } },
    { 0xBB, 1,  { 0x32 } },
    { 0xC2, 1,  { 0x01 } },
    { 0xC3, 1,  { 0x15 } },
    { 0xC4, 1,  { 0x20 } },
    { 0xC6, 1,  { 0x0F } },
    { 0xD0, 2,  { 0xA4, 0xA1 } },
    { 0xE0, 14, { 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34 } },
    { 0xE1, 14, { 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x15, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34 } },
};

/* ---- Public API ---- */

whisplay_config_t whisplay_default_config(void)
{
    whisplay_config_t c = {
        .spi_dev = "/dev/spidev0.0",
        .spi_hz = 100000000,
        .pin_rst = 4,
        .pin_dc = 27,
        .pin_bl = 22,
        .buf_lines = 70,
    };
    return c;
}

lv_display_t *whisplay_display_create(const whisplay_config_t *cfg)
{
    whisplay_config_t def = whisplay_default_config();
    if (!cfg) cfg = &def;

    g.pin_rst = cfg->pin_rst;
    g.pin_dc = cfg->pin_dc;
    g.pin_bl = cfg->pin_bl;
    g.failed = false;

    /* spidev's per-transfer limit: one large transfer is faster than many 4 KB ones */
    FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    if (f) {
        size_t b;
        if (fscanf(f, "%zu", &b) == 1 && b >= 64) g.chunk = b;
        fclose(f);
    }

    g.gpio_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (g.gpio_fd < 0) { perror("whisplay: /dev/gpiomem"); goto fail; }
    g.gpio = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, g.gpio_fd, 0);
    if (g.gpio == MAP_FAILED) { g.gpio = NULL; perror("whisplay: mmap gpiomem"); goto fail; }
    gpio_output(g.pin_rst);
    gpio_output(g.pin_dc);
    gpio_output(g.pin_bl);
    gpio_write(g.pin_bl, 1);   /* backlight off (active low) */

    g.spi_fd = open(cfg->spi_dev, O_RDWR);
    if (g.spi_fd < 0) { perror("whisplay: spidev"); goto fail; }
    uint8_t mode = 0, bpw = 8;
    uint32_t hz = cfg->spi_hz;
    if (ioctl(g.spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(g.spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0 ||
        ioctl(g.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
        perror("whisplay: spidev config");
        goto fail;
    }

    /* hardware reset */
    gpio_write(g.pin_rst, 1); usleep(100000);
    gpio_write(g.pin_rst, 0); usleep(100000);
    gpio_write(g.pin_rst, 1); usleep(120000);

    /* 0xC0 in MADCTL = mirror X and Y, which is how the panel is mounted */
    lv_display_t *d = lv_st7789_create(WHISPLAY_WIDTH, WHISPLAY_HEIGHT,
                                       LV_LCD_FLAG_MIRROR_X | LV_LCD_FLAG_MIRROR_Y,
                                       send_cmd, send_color);
    if (!d) { fprintf(stderr, "whisplay: lv_st7789_create failed\n"); goto fail; }
    lv_st7789_set_gap(d, 0, 20);       /* rounded-corner rows: RAM starts 20 lines above the visible area */
    lv_st7789_set_invert(d, true);
    for (unsigned i = 0; i < sizeof(tuning) / sizeof(*tuning); i++)
        send_cmd(d, &tuning[i].cmd, 1, tuning[i].p, tuning[i].n);

    int lines = cfg->buf_lines > 0 ? cfg->buf_lines : 70;
    size_t size = (size_t)WHISPLAY_WIDTH * lines * 2;
    size = (size + 63) & ~(size_t)63;
    g.buf = aligned_alloc(64, size);
    if (!g.buf) { fprintf(stderr, "whisplay: out of memory\n"); goto fail; }
    lv_display_set_buffers(d, g.buf, NULL, size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    if (g.failed) goto fail;
    return d;

fail:
    whisplay_display_destroy();
    return NULL;
}

void whisplay_backlight(bool on)
{
    if (g.gpio) gpio_write(g.pin_bl, on ? 0 : 1);
}

void whisplay_display_destroy(void)
{
    whisplay_backlight(false);
    if (g.gpio) { munmap((void *)g.gpio, 4096); g.gpio = NULL; }
    if (g.gpio_fd >= 0) { close(g.gpio_fd); g.gpio_fd = -1; }
    if (g.spi_fd >= 0) { close(g.spi_fd); g.spi_fd = -1; }
    free(g.buf);
    g.buf = NULL;
}

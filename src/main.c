#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "sysinfo.h"
#include "whisplay_display.h"

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static uint32_t tick_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void set_bar(void *bar, int32_t v)
{
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
}

/* ---- Status bar: row 1 CPU / MEM / temperature, row 2 IP address ---- */

static lv_obj_t *cpu_label, *mem_label, *temp_label, *ip_label;

static lv_obj_t *status_label(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t w,
                              lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

/* "CPU 42%": the name and the value are separate labels so the value can change width */
static lv_obj_t *make_metric(lv_obj_t *parent, const char *name, int32_t x, int32_t y)
{
    status_label(parent, name, x, y, LV_SIZE_CONTENT, LV_TEXT_ALIGN_LEFT);   /* fixed widths wrapped the M */
    return status_label(parent, "0%", x + 26, y, 24, LV_TEXT_ALIGN_LEFT);
}

static void set_metric(lv_obj_t *label, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_label_set_text_fmt(label, "%d%%", pct);
}

static void update_status(lv_timer_t *t)
{
    (void)t;
    set_metric(cpu_label, sysinfo_cpu_percent());

    int mem = sysinfo_mem_percent();
    if (mem >= 0) set_metric(mem_label, mem);

    float temp;
    if (sysinfo_temp_c(&temp)) {
        int deci = (int)(temp * 10 + (temp < 0 ? -0.5f : 0.5f));   /* lv_label_set_text_fmt has no %f */
        lv_label_set_text_fmt(temp_label, "%d.%dC", deci / 10, deci % 10 < 0 ? -(deci % 10) : deci % 10);
    } else {
        lv_label_set_text(temp_label, "--C");
    }

    char ip[16];
    lv_label_set_text(ip_label, sysinfo_ip(ip, sizeof ip) ? ip : "N/A");
}

static void build_status_bar(lv_obj_t *scr)
{
    /* the panel corners are rounded, keep clear of them */
    cpu_label = make_metric(scr, "CPU", 14, 6);
    mem_label = make_metric(scr, "MEM", 104, 6);
    temp_label = status_label(scr, "--C", 188, 6, 40, LV_TEXT_ALIGN_LEFT);

    ip_label = status_label(scr, "", 0, 0, 212, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(ip_label, LV_ALIGN_BOTTOM_MID, 0, -4);   /* stays at the bottom when the font changes height */

    update_status(NULL);              /* the first CPU reading is 0, the timer fills in the rest */
    lv_timer_create(update_status, 1000, NULL);
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    build_status_bar(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Coney");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 160, 12);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(bar, 0, 100);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, set_bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_playback_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *disp = whisplay_display_create(NULL);
    if (!disp) return 1;

    build_ui();
    lv_timer_handler();      /* draw the first frame before the backlight comes on */
    whisplay_backlight(true);

    while (running) {
        uint32_t wait = lv_timer_handler();
        if (wait > 50) wait = 50;
        if (wait < 1) wait = 1;
        usleep(wait * 1000);
    }

    whisplay_display_destroy();
    return 0;
}

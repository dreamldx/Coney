#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
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

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

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

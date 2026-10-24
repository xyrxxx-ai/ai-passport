// main/pupu_app.c —— 噗噗的主循环(一个 lv_timer,所以只在 LVGL 任务里运行)。
//
// 每 50 ms:取按键事件 → pupu_input 翻译成动作 → 交给当前页面;真实时间差 → pupu_clock
// 换算成宠物秒 → pupu_model 推进;最后更新当前页面。
//
// M2 还没有存档(M3 做),所以每次开机都是一颗新蛋。
#include "pupu_app.h"

#include "pupu_clock.h"
#include "pupu_config.h"
#include "pupu_fonts.h"
#include "pupu_input.h"
#include "pupu_model.h"
#include "pupu_sprite_math.h"
#include "pupu_ui.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include <inttypes.h>

_Static_assert((int)BSP_BTN_UP == PUPU_KEY_UP && (int)BSP_BTN_DOWN == PUPU_KEY_DOWN &&
               (int)BSP_BTN_OK == PUPU_KEY_OK, "pupu_key_t must match bsp_btn_t");
_Static_assert((int)BSP_BTN_PRESS == PUPU_KEV_PRESS && (int)BSP_BTN_CLICK == PUPU_KEV_CLICK &&
               (int)BSP_BTN_DOUBLE == PUPU_KEV_DOUBLE && (int)BSP_BTN_LONG == PUPU_KEV_LONG &&
               (int)BSP_BTN_RELEASE == PUPU_KEV_RELEASE, "pupu_key_ev_t must match bsp_btn_ev_t");

static const char *TAG = "pupu_app";

#define LOOP_MS 50
#define KEY_QUEUE_DEPTH 16
#define STATUS_LOG_MS 60000

typedef enum { PAGE_EGG, PAGE_HOME } page_t;

typedef struct {
    uint8_t btn;
    uint8_t ev;
    uint32_t t_ms;  // 按键回调收到它的时间
} key_msg_t;

static QueueHandle_t s_keys;
static volatile uint32_t s_dropped;  // 队列满时丢掉的按键事件
static uint32_t s_dropped_logged;

static pupu_model_t s_model;
static pupu_clock_t s_clock;
static pupu_input_t s_input;
static page_t s_page;
static lv_obj_t *s_screen;
static int64_t s_last_us;
static uint32_t s_status_ms;

static uint32_t ms_now(int64_t us) {
    return (uint32_t)(us / 1000);
}

static void log_mem(const char *where) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "内存[%s] LVGL 池 已用 %d%% 峰值 %" PRIu32 " B 最大空块 %" PRIu32
                  " B | 堆 %" PRIu32 " B | 内部 DMA 最大块 %" PRIu32 " B",
             where, (int)mon.used_pct, (uint32_t)mon.max_used, (uint32_t)mon.free_biggest_size,
             esp_get_free_heap_size(),
             (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

static void show_page(page_t page, uint32_t now_ms) {
    lv_obj_t *old = s_screen;
    pupu_input_cancel_hold(&s_input);
    s_page = page;
    s_screen = page == PAGE_EGG ? pupu_ui_egg_create(now_ms) : pupu_ui_home_create(&s_model, now_ms);
    lv_screen_load(s_screen);
    if (old) lv_obj_delete(old);  // 同一时间只保留一个页面
    log_mem(page == PAGE_EGG ? "egg" : "home");
}

static void handle_action(pupu_action_t act, uint32_t now_ms) {
    if (act == PUPU_ACT_NONE) return;
    if (s_page == PAGE_EGG) {
        pupu_ui_egg_action(act, &s_model, now_ms);
    } else {
        pupu_ui_home_action(act, now_ms);
    }
}

static void log_status(void) {
    ESP_LOGI(TAG, "宠物 阶段=%d 饱腹=%d 开心=%d 亲密=%d 毛色档=%d 活了 %" PRIu32 " 宠物分钟",
             s_model.stage, s_model.full, s_model.happy, s_model.bond,
             pupu_tint_step(s_model.full, s_model.happy), s_model.life_min);
}

static void loop_cb(lv_timer_t *t) {
    (void)t;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t now_ms = ms_now(now_us);
    const pupu_input_mode_t mode = s_page == PAGE_EGG ? PUPU_INPUT_EGG : PUPU_INPUT_MENU;

    key_msg_t k;
    while (xQueueReceive(s_keys, &k, 0) == pdTRUE) {
        handle_action(pupu_input_key(&s_input, mode, (pupu_key_t)k.btn, (pupu_key_ev_t)k.ev, k.t_ms),
                      now_ms);
    }
    handle_action(pupu_input_tick(&s_input, now_ms), now_ms);

    const uint32_t pet_s = pupu_clock_tick(&s_clock, now_us - s_last_us);
    s_last_us = now_us;
    if (pet_s > 0) pupu_model_advance(&s_model, pet_s);

    if (s_page == PAGE_EGG) {
        if (pupu_ui_egg_update(&s_input, now_ms)) show_page(PAGE_HOME, now_ms);
    } else {
        pupu_ui_home_update(&s_model, now_ms);
    }

    const uint32_t dropped = s_dropped;
    if (dropped != s_dropped_logged) {
        ESP_LOGW(TAG, "按键队列满,累计丢了 %" PRIu32 " 个事件", dropped);
        s_dropped_logged = dropped;
    }
    if (now_ms - s_status_ms >= STATUS_LOG_MS) {
        s_status_ms = now_ms;
        log_status();
    }
}

esp_err_t pupu_app_start(void) {
    s_keys = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_msg_t));
    if (s_keys == NULL) return ESP_ERR_NO_MEM;

    pupu_fonts_init();
    pupu_fonts_selfcheck();

    pupu_model_init(&s_model);
    pupu_clock_init(&s_clock, CONFIG_PUPU_TIME_SCALE, 0);
    pupu_input_init(&s_input);
    s_last_us = esp_timer_get_time();
    s_status_ms = ms_now(s_last_us);
    ESP_LOGI(TAG, "新蛋(存档在 M3 才做),时间倍率 %d", CONFIG_PUPU_TIME_SCALE);

    log_mem("boot");
    show_page(PAGE_EGG, ms_now(s_last_us));
    if (lv_timer_create(loop_cb, LOOP_MS, NULL) == NULL) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool pupu_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (s_keys == NULL) return false;
    const key_msg_t k = {
        .btn = (uint8_t)btn,
        .ev = (uint8_t)ev,
        .t_ms = ms_now(esp_timer_get_time()),
    };
    if (xQueueSend(s_keys, &k, 0) != pdTRUE) {
        s_dropped++;
        return false;
    }
    return true;
}

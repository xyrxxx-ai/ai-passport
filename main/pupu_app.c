// main/pupu_app.c —— 噗噗的主循环(一个 lv_timer,所以只在 LVGL 任务里运行)。
//
// 每 50 ms:取按键事件 → pupu_power(熄屏时亮屏、吞掉多余按键)→ pupu_input 翻译成动作
// → 交给当前页面;真实时间差 → pupu_clock 换算成宠物秒 → pupu_model 推进;pupu_power
// 决定变暗、熄屏、深睡;更新当前页面(熄屏时不画);到时间就存档(时机见 pupu_save_policy.h)。
//
// 开机时从 NVS 读存档。如果 RTC 一直在走(深睡醒来、USB 或软件重启,见 pupu_clock.h),
// 就按离开的时长补算,超过 1 小时显示"你回来啦";断过电时宠物保持存档时的样子。
// 深睡交给 pupu_ctl 工作任务去做,醒来等于重新开机。
// 同步页(pupu_ui_sync.c)开着蓝牙窗口,收到手机的对时也按离开时长补算(pupu_sync_proto.h)。
#include "pupu_app.h"

#include "pupu_clock.h"
#include "pupu_config.h"
#include "pupu_ctl.h"
#include "pupu_fonts.h"
#include "pupu_input.h"
#include "pupu_model.h"
#include "pupu_power.h"
#include "pupu_save_policy.h"
#include "pupu_sfx.h"
#include "pupu_sprite_math.h"
#include "pupu_store.h"
#include "pupu_text.h"
#include "pupu_ui.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rtc_time.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdio.h>

_Static_assert((int)BSP_BTN_UP == PUPU_KEY_UP && (int)BSP_BTN_DOWN == PUPU_KEY_DOWN &&
               (int)BSP_BTN_OK == PUPU_KEY_OK, "pupu_key_t must match bsp_btn_t");
_Static_assert((int)BSP_BTN_PRESS == PUPU_KEV_PRESS && (int)BSP_BTN_CLICK == PUPU_KEV_CLICK &&
               (int)BSP_BTN_DOUBLE == PUPU_KEV_DOUBLE && (int)BSP_BTN_LONG == PUPU_KEV_LONG &&
               (int)BSP_BTN_RELEASE == PUPU_KEV_RELEASE, "pupu_key_ev_t must match bsp_btn_ev_t");
_Static_assert((int)ESP_RST_UNKNOWN == PUPU_RESET_UNKNOWN && (int)ESP_RST_POWERON == PUPU_RESET_POWERON &&
               (int)ESP_RST_EXT == PUPU_RESET_EXT && (int)ESP_RST_SW == PUPU_RESET_SW &&
               (int)ESP_RST_PANIC == PUPU_RESET_PANIC && (int)ESP_RST_INT_WDT == PUPU_RESET_INT_WDT &&
               (int)ESP_RST_TASK_WDT == PUPU_RESET_TASK_WDT && (int)ESP_RST_WDT == PUPU_RESET_WDT &&
               (int)ESP_RST_DEEPSLEEP == PUPU_RESET_DEEPSLEEP &&
               (int)ESP_RST_BROWNOUT == PUPU_RESET_BROWNOUT && (int)ESP_RST_SDIO == PUPU_RESET_SDIO &&
               (int)ESP_RST_USB == PUPU_RESET_USB && (int)ESP_RST_JTAG == PUPU_RESET_JTAG &&
               (int)ESP_RST_EFUSE == PUPU_RESET_EFUSE &&
               (int)ESP_RST_PWR_GLITCH == PUPU_RESET_PWR_GLITCH &&
               (int)ESP_RST_CPU_LOCKUP == PUPU_RESET_CPU_LOCKUP,
               "pupu_reset_reason_t must match esp_reset_reason_t");

static const char *TAG = "pupu_app";

#define LOOP_MS 50
#define GAME_LOOP_MS 20  // 小游戏页:每 20 ms 一次(catch_model 的一步)
#define KEY_QUEUE_DEPTH 16
#define STATUS_LOG_MS 60000
#define BATTERY_EVERY_MS 30000  // 多久读一次电量(一次 I2C 读,约 1 ms)

typedef enum { PAGE_EGG, PAGE_HOME, PAGE_FEED, PAGE_PLAY, PAGE_STATUS, PAGE_CATCH, PAGE_SYNC } page_t;
static const char *const k_page_names[] = {"egg", "home", "feed", "play", "status", "catch", "sync"};

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
static pupu_power_t s_power;
static pupu_autosave_t s_autosave;
static pupu_sync_t s_sync;  // 同步协议的状态;离开分钟数要跨窗口保留,所以开机时只初始化一次
static void sync_snapshot(pupu_save_t *out);
static const pupu_sync_env_t s_sync_env = {&s_model, &s_clock, sync_snapshot};
static bool s_sleeping;  // 深睡交给了 pupu_ctl:等它睡下去,或者放弃
// 会话令牌(见 pupu_clock.h)。RTC_NOINIT 变量深睡、软件重启后都还在,刚通电时是乱码。
static uint64_t s_token;
static RTC_NOINIT_ATTR uint64_t s_rtc_token;
static char s_welcome[64];  // "你回来啦"的文字
static page_t s_page;
static lv_obj_t *s_screen;
static lv_timer_t *s_loop;
static uint32_t s_key_ms;  // 正在处理的动作来自哪个按键时间(按键回调记下的)
static const char *s_notice;  // 开机提示,交给第一个页面显示
static int64_t s_last_us;
static uint32_t s_status_ms;
static int s_battery = -1;  // 电量百分比,负数表示读不到
static uint32_t s_battery_ms;

static uint32_t ms_now(int64_t us) {
    return (uint32_t)(us / 1000);
}

static void log_mem(const char *where) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "内存[%s] LVGL 池 已用 %d%% 峰值 %" PRIu32 " B 最大空块 %" PRIu32
                  " B | 堆 %" PRIu32 " B 历史最低 %" PRIu32 " B | 内部 DMA 最大块 %" PRIu32 " B",
             where, (int)mon.used_pct, (uint32_t)mon.max_used, (uint32_t)mon.free_biggest_size,
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
             (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

static void show_page(page_t page, uint32_t now_ms) {
    lv_obj_t *old = s_screen;
    pupu_input_cancel_hold(&s_input);
    if (s_page == PAGE_CATCH && old) pupu_ui_catch_leave();  // 没结束的一局作废
    s_page = page;
    switch (page) {
    case PAGE_EGG: s_screen = pupu_ui_egg_create(s_notice, now_ms); break;
    case PAGE_HOME: s_screen = pupu_ui_home_create(&s_model, s_battery, s_notice, now_ms); break;
    case PAGE_FEED: s_screen = pupu_ui_care_create(PUPU_CARE_FEED, &s_model, now_ms); break;
    case PAGE_PLAY: s_screen = pupu_ui_care_create(PUPU_CARE_PLAY, &s_model, now_ms); break;
    case PAGE_CATCH: s_screen = pupu_ui_catch_create(&s_model, now_ms); break;
    case PAGE_SYNC: s_screen = pupu_ui_sync_create(&s_sync_env, &s_sync, now_ms); break;
    default: s_screen = pupu_ui_status_create(&s_model); break;
    }
    s_notice = NULL;
    lv_screen_load(s_screen);
    if (old) lv_obj_delete(old);  // 同一时间只保留一个页面
    if (s_loop) lv_timer_set_period(s_loop, page == PAGE_CATCH ? GAME_LOOP_MS : LOOP_MS);
    log_mem(k_page_names[page]);
}

static void update_page(uint32_t now_ms);
static void apply_power(pupu_power_do_t todo, uint32_t now_ms);
static void log_status(void);

// 补算的离开时长(宠物时间)超过 1 小时:下一个页面显示"你回来啦！离开了 X 小时"。
static void note_welcome(uint64_t away_pet_s) {
    if (away_pet_s < PUPU_WELCOME_AWAY_S || s_notice != NULL) return;
    const uint64_t hours = away_pet_s / 3600u;
    snprintf(s_welcome, sizeof(s_welcome), PUPU_TXT_WELCOME, (unsigned)(hours > 9999u ? 9999u : hours));
    s_notice = s_welcome;
}

static void handle_req(pupu_ui_req_t req, uint32_t now_ms) {
    switch (req) {
    case PUPU_UI_CHANGED: pupu_autosave_mark(&s_autosave, now_ms); break;
    case PUPU_UI_GO_HOME:
        if (s_page == PAGE_SYNC) note_welcome(pupu_ui_sync_away_pet_s());  // 同步时补算过
        show_page(PAGE_HOME, now_ms);
        break;
    case PUPU_UI_GO_FEED: show_page(PAGE_FEED, now_ms); break;
    case PUPU_UI_GO_PLAY: show_page(PAGE_PLAY, now_ms); break;
    case PUPU_UI_GO_STATUS: show_page(PAGE_STATUS, now_ms); break;
    case PUPU_UI_GO_CATCH: show_page(PAGE_CATCH, now_ms); break;
    case PUPU_UI_GO_SYNC: show_page(PAGE_SYNC, now_ms); break;
    case PUPU_UI_SCREEN_OFF: apply_power(pupu_power_screen_off(&s_power, now_ms), now_ms); break;
    default: break;
    }
}

static void handle_action(pupu_action_t act, uint32_t now_ms) {
    if (act == PUPU_ACT_NONE) return;
    pupu_ui_req_t req;
    switch (s_page) {
    case PAGE_EGG: req = pupu_ui_egg_action(act, &s_model, now_ms); break;
    case PAGE_HOME: req = pupu_ui_home_action(act, now_ms); break;
    case PAGE_FEED:
    case PAGE_PLAY: req = pupu_ui_care_action(act, &s_model, s_clock.acct_now, now_ms); break;
    case PAGE_CATCH: req = pupu_ui_catch_action(act, &s_model, s_key_ms, now_ms); break;
    case PAGE_SYNC: req = pupu_ui_sync_action(act, now_ms); break;
    default: req = pupu_ui_status_action(act); break;
    }
    handle_req(req, now_ms);
}

static void update_page(uint32_t now_ms) {
    switch (s_page) {
    case PAGE_EGG: handle_req(pupu_ui_egg_update(&s_input, now_ms), now_ms); break;
    case PAGE_HOME: pupu_ui_home_update(&s_model, s_battery, now_ms); break;
    case PAGE_FEED:
    case PAGE_PLAY: pupu_ui_care_update(&s_model, now_ms); break;
    case PAGE_CATCH: handle_req(pupu_ui_catch_update(&s_model, now_ms), now_ms); break;
    case PAGE_SYNC: handle_req(pupu_ui_sync_update(now_ms), now_ms); break;
    default: pupu_ui_status_update(&s_model); break;
    }
}

static pupu_save_t snapshot(void) {
    pupu_save_t s = {0};
    s.model = s_model;
    s.acct_epoch = s_clock.acct_now;
    s.rtc_us = esp_rtc_get_time_us();
    s.token = s_token;
    return s;
}

static void sync_snapshot(pupu_save_t *out) {
    *out = snapshot();
}

static void save_now(uint32_t now_ms) {
    const pupu_save_t s = snapshot();
    pupu_store_save(&s);
    pupu_autosave_done(&s_autosave, now_ms);
}

// 现在不适合变暗、熄屏或深睡(见 pupu_power.h)。
static bool app_busy(void) {
    if (pupu_sfx_busy() || pupu_store_busy()) return true;
    switch (s_page) {
    case PAGE_EGG: return s_input.holding || pupu_ui_egg_busy();
    case PAGE_FEED:
    case PAGE_PLAY: return pupu_ui_care_busy();
    case PAGE_CATCH: return pupu_ui_catch_busy();  // 暂停不算忙
    case PAGE_SYNC: return pupu_ui_sync_busy();    // 蓝牙开着、正在开或正在关
    default: return false;
    }
}

static pupu_input_mode_t input_mode(void) {
    if (s_page == PAGE_EGG) return PUPU_INPUT_EGG;
    if (s_page == PAGE_CATCH && pupu_ui_catch_in_round()) return PUPU_INPUT_GAME;
    return PUPU_INPUT_MENU;
}

static void set_panel_on(bool on) {
    const esp_err_t err = esp_lcd_panel_disp_on_off(bsp_display_panel(), on);
    if (err != ESP_OK) ESP_LOGW(TAG, "DISP%s 失败: %s", on ? "ON" : "OFF", esp_err_to_name(err));
}

static void apply_power(pupu_power_do_t todo, uint32_t now_ms) {
    switch (todo) {
    case PUPU_PWR_DO_DIM:
        ESP_LOGI(TAG, "变暗");
        bsp_display_backlight(PUPU_DIM_BACKLIGHT);
        pupu_autosave_request(&s_autosave, now_ms);
        break;
    case PUPU_PWR_DO_UNDIM:
        ESP_LOGI(TAG, "恢复亮度");
        bsp_display_backlight(100);
        break;
    case PUPU_PWR_DO_OFF:
        ESP_LOGI(TAG, "熄屏");
        // 小游戏暂停时熄屏:这一局作废(show_page 会调 pupu_ui_catch_leave),亮屏回到玩耍页
        if (s_page == PAGE_CATCH) show_page(PAGE_PLAY, now_ms);
        pupu_input_init(&s_input);  // 丢掉按着的键和正在捂热的蛋;熄屏时的松开不会再传过来
        set_panel_on(false);        // DISPOFF:屏幕显存保留
        bsp_display_backlight(0);
        pupu_autosave_request(&s_autosave, now_ms);
        log_mem("off");
        break;
    case PUPU_PWR_DO_WAKE:
        ESP_LOGI(TAG, "亮屏");
        pupu_input_init(&s_input);
        // 同步页熄过屏,说明窗口早就关了、蓝牙也关了:亮屏直接回首页,把同步时的
        // "你回来啦"带过去(熄屏期间不显示,否则气泡的 4 秒在黑屏里就过完了)
        if (s_page == PAGE_SYNC) handle_req(PUPU_UI_GO_HOME, now_ms);
        update_page(now_ms);  // 熄屏期间没画:先把这一帧画好,再打开屏幕
        lv_refr_now(NULL);
        set_panel_on(true);
        bsp_display_backlight(100);
        break;
    case PUPU_PWR_DO_SLEEP: {
        ESP_LOGI(TAG, "熄屏已满 %" PRIu32 " 秒,准备深睡", (uint32_t)(PUPU_SLEEP_AFTER_OFF_MS / 1000));
        log_status();
        log_mem("sleep");
        const pupu_save_t s = snapshot();
        s_sleeping = true;  // 先停下主循环里的 I2C 和存档,再交出命令
        // 交给了工作任务才算存过;工作任务不可用时定时存档照常进行
        if (pupu_ctl_deep_sleep(&s)) pupu_autosave_done(&s_autosave, now_ms);
        // 交出命令之前刚好有按键进了队列:放弃这一次(之后的按键由 pupu_ctl_note_key 记下)
        if (uxQueueMessagesWaiting(s_keys) > 0) pupu_ctl_note_key();
        break;
    }
    default:
        break;
    }
}

static void log_status(void) {
    ESP_LOGI(TAG, "宠物 阶段=%d 饱腹=%d 开心=%d 亲密=%d 星星糖=%d 毛色档=%d 活了 %" PRIu32
                  " 宠物分钟",
             s_model.stage, s_model.full, s_model.happy, s_model.bond, s_model.candy,
             pupu_tint_step(s_model.full, s_model.happy), s_model.life_min);
}

static void loop_cb(lv_timer_t *t) {
    (void)t;
    int64_t now_us = esp_timer_get_time();
    uint32_t now_ms = ms_now(now_us);

    if (s_sleeping) {
        // 深睡交给了 pupu_ctl:它会关掉 I2C、音频和屏幕,这里什么都不碰。按键留在队列里。
        if (!pupu_ctl_take_sleep_refused()) return;
        ESP_LOGW(TAG, "深睡被放弃,%" PRIu32 " 秒后再试", (uint32_t)(PUPU_SLEEP_RETRY_MS / 1000));
        s_sleeping = false;
        pupu_power_sleep_refused(&s_power, now_ms);
        // s_last_us 没动:下面照常补上等待期间的宠物时间
    }

    key_msg_t k;
    while (xQueueReceive(s_keys, &k, 0) == pdTRUE) {
        pupu_power_do_t todo;
        const bool swallowed =
            pupu_power_key(&s_power, (pupu_key_t)k.btn, (pupu_key_ev_t)k.ev, k.t_ms, &todo);
        apply_power(todo, now_ms);
        if (swallowed) continue;
        s_key_ms = k.t_ms;
        handle_action(pupu_input_key(&s_input, input_mode(), (pupu_key_t)k.btn, (pupu_key_ev_t)k.ev,
                                     k.t_ms),
                      now_ms);
    }
    // 处理按键时(比如亮屏时先画一帧)按键回调可能又放进来带着更晚时间的事件。重新读一次时间,
    // 保证下面的计时(蛋的 3 秒、pupu_power)用的 now 不早于任何一个处理过的按键。
    now_us = esp_timer_get_time();
    now_ms = ms_now(now_us);
    s_key_ms = now_ms;
    handle_action(pupu_input_tick(&s_input, now_ms), now_ms);

    const uint32_t pet_s = pupu_clock_tick(&s_clock, now_us - s_last_us);
    s_last_us = now_us;
    if (pet_s > 0) pupu_model_advance(&s_model, pet_s);

    if (pupu_ctl_battery_ready() && now_ms - s_battery_ms >= BATTERY_EVERY_MS) {
        s_battery_ms = now_ms;
        s_battery = bsp_battery_soc();
        pupu_autosave_battery(&s_autosave, s_battery, now_ms);
    }
    if (s_power.stage != PUPU_PWR_OFF) update_page(now_ms);  // 熄屏时不画

    // 音效还在放时不写 Flash,避免爆音(见 pupu_sfx.h);小游戏一局没结束时也不存,结算后再存
    const bool hold_save = pupu_sfx_busy() || (s_page == PAGE_CATCH && pupu_ui_catch_in_round());
    if (pupu_autosave_due(&s_autosave, now_ms, hold_save)) save_now(now_ms);

    // 放在存档后面:进 DIM/OFF 时要求的那次存档,下一圈才写;深睡用的是同步存档。
    apply_power(pupu_power_tick(&s_power, now_ms, app_busy()), now_ms);
    if (s_sleeping) return;

    const uint32_t dropped = s_dropped;
    if (dropped != s_dropped_logged) {
        ESP_LOGW(TAG, "按键队列满,累计丢了 %" PRIu32 " 个事件", dropped);
        s_dropped_logged = dropped;
    }
    if (now_ms - s_status_ms >= STATUS_LOG_MS) {
        s_status_ms = now_ms;
        log_status();
        log_mem("tick");
    }
}

static uint64_t new_token(void) {
    uint64_t t;
    do {
        t = ((uint64_t)esp_random() << 32) | esp_random();
    } while (t == 0);  // 0 不是有效令牌
    return t;
}

// 开机补算(开发计划 4.3)。RTC 连续时按离开时长补算并沿用令牌,返回 true;
// 否则当作断过电:宠物保持存档时的样子,换新令牌,返回 false。
static bool catch_up_at_boot(const pupu_save_t *save) {
    const pupu_reset_reason_t reason = (pupu_reset_reason_t)esp_reset_reason();
    const pupu_boot_info_t info = {
        .reason = reason,
        .saved_token = save->token,
        .rtc_token = s_rtc_token,
        .saved_rtc_us = save->rtc_us,
        .now_rtc_us = esp_rtc_get_time_us(),
    };
    uint64_t away_s = 0;
    const bool continued = pupu_clock_boot(&s_clock, &info, &away_s);
    s_token = continued ? save->token : new_token();
    s_rtc_token = s_token;
    if (!continued) {
        ESP_LOGI(TAG, "开机原因 %d:RTC 不连续(断过电或第一次用新令牌),宠物保持存档时的样子",
                 (int)reason);
        return false;
    }
    const uint64_t away_pet_s = pupu_clock_to_pet(&s_clock, away_s);
    const pupu_catch_up_t cu = pupu_model_catch_up(&s_model, away_pet_s);
    ESP_LOGI(TAG, "开机原因 %d:RTC 连续,离开 %" PRIu64 " 秒(宠物 %" PRIu64 " 秒),补算 %" PRIu32
                  " 宠物秒,跨过 %" PRIu32 " 天",
             (int)reason, away_s, away_pet_s, cu.applied_s, cu.days);
    pupu_sync_note_away(&s_sync, away_pet_s);  // 手机同步时 STATUS 里的 away_min
    note_welcome(away_pet_s);
    return true;
}

esp_err_t pupu_app_start(void) {
    s_keys = xQueueCreate(KEY_QUEUE_DEPTH, sizeof(key_msg_t));
    if (s_keys == NULL) return ESP_ERR_NO_MEM;

    pupu_fonts_init();
    pupu_fonts_selfcheck();
    pupu_sync_init(&s_sync);

    pupu_save_t save;
    const pupu_store_boot_t boot = pupu_store_start(&save);
    if (boot.loaded) {
        s_model = save.model;
    } else {
        pupu_model_init(&s_model);
    }
    pupu_clock_init(&s_clock, CONFIG_PUPU_TIME_SCALE, boot.loaded ? save.acct_epoch : 0);
    s_notice = boot.locked ? PUPU_TXT_SAVE_LOCKED : boot.corrupt ? PUPU_TXT_SAVE_BROKEN : NULL;
    ESP_LOGI(TAG, "%s,时间倍率 %d", boot.loaded ? "读到存档" : "新蛋", CONFIG_PUPU_TIME_SCALE);
    if (boot.loaded) {
        (void)catch_up_at_boot(&save);
    } else {
        s_token = new_token();
        s_rtc_token = s_token;
    }
    log_status();

    pupu_input_init(&s_input);
    s_last_us = esp_timer_get_time();
    const uint32_t now_ms = ms_now(s_last_us);
    pupu_power_init(&s_power, now_ms);
    s_status_ms = now_ms;
    s_battery_ms = now_ms - BATTERY_EVERY_MS;  // 电量计一就绪就读一次
    s_battery = -1;
    pupu_autosave_init(&s_autosave, now_ms);
    // 每次开机都尽快存一次:换了新令牌或者刚补算过时,下次开机才能接上
    pupu_autosave_mark(&s_autosave, now_ms);

    log_mem("boot");
    show_page(s_model.stage == PUPU_STAGE_EGG ? PAGE_EGG : PAGE_HOME, now_ms);
    pupu_ctl_battery_init();  // 先画出宠物,再初始化电量计(它至少要等 100 ms)
    s_loop = lv_timer_create(loop_cb, LOOP_MS, NULL);
    if (s_loop == NULL) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool pupu_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (s_keys == NULL) return false;
    const key_msg_t k = {
        .btn = (uint8_t)btn,
        .ev = (uint8_t)ev,
        .t_ms = ms_now(esp_timer_get_time()),
    };
    const bool queued = xQueueSend(s_keys, &k, 0) == pdTRUE;
    if (!queued) s_dropped++;
    pupu_ctl_note_key();  // 放进队列以后再记:正在准备深睡时,有按键就放弃这次深睡
    return queued;
}

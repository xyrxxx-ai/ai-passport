// main/pupu_ui_sync.c —— 同步页:打开蓝牙、开一个同步窗口、回答手机的请求。
//
// 进页面就让 pupu_ctl 打开蓝牙;打开以后开始数窗口(规则在 pupu_sync_proto.h):
// 60 秒内等手机来连;连上过以后,等到 DONE,或者 20 秒没有新请求就结束;整次最长 2 分钟。
// 窗口结束(或者长按 OK)时让 pupu_ctl 关蓝牙,关好以前不能离开这一页
// (产品说明第 12 节第 16 条;"蓝牙 + 别的页面"同时运行时的内存没有测过)。
//
// 蓝牙回调只把字节放进队列(pupu_ble.c);这里在主循环里取出来交给 pupu_sync_handle(),
// 再把回复发回去。收到 DONE:回 DONE_ACK、噗噗挥手、"叮"一声,1.5 秒后关窗口。
// ↑/↓ 没有动作;长按 OK 关蓝牙并回首页。
#include "pupu_ui.h"

#include "pupu_ble.h"
#include "pupu_config.h"
#include "pupu_ctl.h"
#include "pupu_pet_view.h"
#include "pupu_sfx.h"
#include "pupu_sprite_math.h"
#include "pupu_text.h"
#include "esp_log.h"

#include <inttypes.h>
#include <stdio.h>

static const char *TAG = "pupu_sync";

#define NAME_Y 222
#define HINT_Y 244
#define WAVE_MS PUPU_SYNC_DONE_LINGER_MS
#define STUCK_RETRY_MS 5000  // 蓝牙关不掉时,隔这么久自动再试

typedef enum { PH_STARTING, PH_OPEN, PH_CLOSING, PH_CLOSED } phase_t;

// 窗口为什么结束(决定结果气泡写什么)。
typedef enum {
    END_NONE = 0,
    END_DONE,
    END_NO_PHONE,
    END_LOST,          // 连上过,但 20 秒没有请求,或者到了 2 分钟
    END_START_FAILED,  // 蓝牙打不开
    END_ERROR,         // 开着的时候出错(广播失败、主机复位)
    END_USER,          // 长按 OK
} end_t;

static const pupu_sync_env_t *s_env;
static pupu_sync_t *s_sync;
static pupu_sync_window_t s_win;
static phase_t s_phase;
static end_t s_end;
static bool s_leave;       // 长按过 OK:蓝牙关好就回首页
static bool s_stuck;       // 蓝牙关不掉:算忙碌(不熄屏、不深睡),自动再试,长按也能马上再试
static uint32_t s_stuck_ms;
static bool s_linked;      // 手机现在连着
static bool s_waving;
static uint32_t s_wave_until_ms;
static uint64_t s_away_pet_s;    // 这个窗口里对时补算的离开时长(宠物秒)
static const char *s_note;       // 临时气泡("你回来啦"、"长大啦!")
static uint32_t s_note_until_ms;
static char s_welcome[2][64];  // 轮流用,保证气泡每次拿到新指针(见 pupu_bubble_show)
static int s_welcome_i;
static char s_secs[16];
static unsigned s_shown_secs;

static pupu_bubble_t s_bubble;
static lv_obj_t *s_secs_label;
static lv_obj_t *s_name_label;
static lv_obj_t *s_hint_label;

static void begin_close(end_t why) {
    if (s_end == END_NONE) s_end = why;
    s_phase = PH_CLOSING;
    s_stuck = false;
    ESP_LOGI(TAG, "关窗口(原因 %d)", (int)why);
    pupu_ble_close_soon();      // 马上停止重新广播和收写入,不用等工作任务
    (void)pupu_ctl_ble_stop();  // 交不出去时状态直接变成 STOP_FAILED,下面会处理
}

lv_obj_t *pupu_ui_sync_create(const pupu_sync_env_t *env, pupu_sync_t *sync, uint32_t now_ms) {
    s_env = env;
    s_sync = sync;
    s_phase = PH_STARTING;
    s_end = END_NONE;
    s_leave = false;
    s_stuck = false;
    s_linked = false;
    s_waving = false;
    s_away_pet_s = 0;
    s_note = NULL;
    s_shown_secs = 0;
    pupu_sync_begin_window(sync);

    lv_obj_t *scr = pupu_ui_screen_create();
    pupu_ui_page_frame(scr, PUPU_TXT_MENU_SYNC);
    s_secs_label = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    lv_obj_add_flag(s_secs_label, LV_OBJ_FLAG_HIDDEN);
    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    pupu_bubble_create(&s_bubble, scr);
    s_name_label = pupu_ui_label(scr, pupu_ble_name(), PUPU_COLOR_INK);
    lv_obj_align(s_name_label, LV_ALIGN_TOP_MID, 0, NAME_Y);
    s_hint_label = pupu_ui_label(scr, PUPU_TXT_SYNC_HINT, PUPU_COLOR_MUTED);
    lv_obj_align(s_hint_label, LV_ALIGN_TOP_MID, 0, HINT_Y);
    lv_obj_add_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "打开蓝牙,名字 %s", pupu_ble_name());
    (void)pupu_ctl_ble_start();  // 交不出去时状态直接变成 START_FAILED
    (void)pupu_ui_sync_update(now_ms);
    return scr;
}

pupu_ui_req_t pupu_ui_sync_action(pupu_action_t act, uint32_t now_ms) {
    (void)now_ms;
    if (act != PUPU_ACT_BACK) return PUPU_UI_NONE;  // ↑/↓、OK 在这一页没有动作
    switch (s_phase) {
    case PH_CLOSED:
        if (!s_stuck) return PUPU_UI_GO_HOME;
        s_leave = true;  // 上次没关掉:马上再试一次,关好就回首页
        begin_close(END_USER);
        return PUPU_UI_NONE;
    case PH_OPEN:
        s_leave = true;
        begin_close(END_USER);
        return PUPU_UI_NONE;
    default:  // 正在开或正在关:等它做完(update 里会接着关、再回首页)
        s_leave = true;
        return PUPU_UI_NONE;
    }
}

static void show_note(const char *text, uint32_t now_ms) {
    s_note = text;
    s_note_until_ms = now_ms + PUPU_NOTICE_MS;
}

// 一条手机的请求:交给协议模块,把回复发回去。
static pupu_ui_req_t handle_rx(const pupu_ble_event_t *ev, uint32_t now_ms) {
    pupu_sync_window_request(&s_win, now_ms);
    const pupu_sync_result_t r = pupu_sync_handle(s_sync, s_env, ev->data, ev->len);
    const bool sent = pupu_ble_send(r.reply, r.reply_len);
    ESP_LOGI(TAG, "请求 type=0x%02x seq=%u len=%u → 回复 type=0x%02x(%s)", ev->len ? ev->data[0] : 0,
             ev->len > 1 ? ev->data[1] : 0, ev->len, r.reply[0], sent ? "已发" : "没发出去:手机还没打开通知");
    if (r.done && !s_win.done) {
        pupu_sync_window_done(&s_win, now_ms);
        s_waving = true;
        s_wave_until_ms = now_ms + WAVE_MS;
        s_end = END_DONE;
        pupu_pet_view_play(pupu_ui_pet_anim(s_env->model, PUPU_POSE_WAVE), true, now_ms);
        pupu_sfx_play(PUPU_SFX_SYNC);
        ESP_LOGI(TAG, "收到 DONE:挥手,%u ms 后关窗口", (unsigned)WAVE_MS);
    }
    if (!r.time_accepted) return PUPU_UI_NONE;

    const pupu_model_t *m = s_env->model;
    ESP_LOGI(TAG, "对时:acct_now=%" PRIu32 " 离开 %" PRIu64 " 宠物秒,首次同步亲密 +%u → 饱腹=%d 开心=%d 亲密=%d",
             s_env->clock->acct_now, r.away_pet_s, (unsigned)r.bonus.bond, m->full, m->happy, m->bond);
    if (r.bonus.grew) {
        pupu_sfx_play(PUPU_SFX_GROW);
        show_note(PUPU_TXT_GREW, now_ms);
    } else if (r.away_pet_s >= PUPU_WELCOME_AWAY_S) {
        const uint64_t hours = r.away_pet_s / 3600u;
        s_welcome_i ^= 1;
        snprintf(s_welcome[s_welcome_i], sizeof(s_welcome[0]), PUPU_TXT_WELCOME,
                 (unsigned)(hours > 9999u ? 9999u : hours));
        show_note(s_welcome[s_welcome_i], now_ms);
    }
    if (r.away_pet_s >= PUPU_WELCOME_AWAY_S) s_away_pet_s = r.away_pet_s;
    return PUPU_UI_CHANGED;
}

static pupu_ui_req_t handle_events(uint32_t now_ms) {
    pupu_ui_req_t req = PUPU_UI_NONE;
    // 还没看到"蓝牙已打开"时先不取:广播和很快的连接可能比状态先到,留在队列里下一圈再处理。
    // (pupu_ble_start 在主机任务启动前清空了队列,不会混进上一个窗口的事件。)
    if (s_phase == PH_STARTING) return req;
    pupu_ble_event_t ev;
    while (pupu_ble_poll(&ev)) {
        if (s_phase != PH_OPEN) continue;  // 窗口已经在关:剩下的事件都不要了
        switch (ev.kind) {
        case PUPU_BLE_EV_ADVERTISING:
            ESP_LOGI(TAG, "广播中");
            break;
        case PUPU_BLE_EV_CONNECTED:
            s_linked = true;
            pupu_sync_new_link(s_sync);  // 上一次连接的快照不要了
            pupu_sync_window_link(&s_win, true, now_ms);
            break;
        case PUPU_BLE_EV_DISCONNECTED:
            s_linked = false;
            pupu_sync_window_link(&s_win, false, now_ms);
            break;
        case PUPU_BLE_EV_SUBSCRIBED:  // 从这以后 pupu_ble_send 才会真的发出去
            ESP_LOGI(TAG, "手机打开了通知");
            break;
        case PUPU_BLE_EV_RX:
            if (handle_rx(&ev, now_ms) == PUPU_UI_CHANGED) req = PUPU_UI_CHANGED;
            break;
        default:  // PUPU_BLE_EV_FAILED
            ESP_LOGE(TAG, "蓝牙出错: %d", ev.code);
            begin_close(END_ERROR);
            break;
        }
    }
    return req;
}

static void step(uint32_t now_ms) {
    const pupu_ctl_ble_t ble = pupu_ctl_ble_state();
    switch (s_phase) {
    case PH_STARTING:
        if (ble == PUPU_CTL_BLE_ON) {
            s_phase = PH_OPEN;
            pupu_sync_window_open(&s_win, now_ms);
            ESP_LOGI(TAG, "窗口打开,%u 秒内等手机来连", (unsigned)(PUPU_SYNC_WAIT_MS / 1000));
            if (s_leave) begin_close(END_USER);
        } else if (ble == PUPU_CTL_BLE_START_FAILED) {
            begin_close(END_START_FAILED);  // 也要关一次:启动失败时的清理可能没做完
        }
        break;
    case PH_OPEN: {
        const pupu_win_close_t why = pupu_sync_window_check(&s_win, now_ms);
        if (why == PUPU_WIN_CLOSE_DONE) begin_close(END_DONE);
        else if (why == PUPU_WIN_CLOSE_NO_PHONE) begin_close(END_NO_PHONE);
        else if (why != PUPU_WIN_OPEN) begin_close(END_LOST);
        break;
    }
    case PH_CLOSING:
        if (ble == PUPU_CTL_BLE_OFF) {
            s_phase = PH_CLOSED;
            ESP_LOGI(TAG, "蓝牙关好了");
        } else if (ble == PUPU_CTL_BLE_STOP_FAILED) {
            s_phase = PH_CLOSED;
            s_stuck = true;
            s_stuck_ms = now_ms;
            s_leave = false;  // 关不掉就先留在这一页
        }
        break;
    default:  // PH_CLOSED
        if (s_stuck && now_ms - s_stuck_ms >= STUCK_RETRY_MS) begin_close(END_NONE);
        break;
    }
}

static const char *bubble_text(void) {
    if (s_stuck) return PUPU_TXT_SYNC_STUCK;
    if (s_end == END_DONE) return PUPU_TXT_SYNC_DONE;  // 挥手、关蓝牙、关好以后都显示
    switch (s_phase) {
    case PH_STARTING: return PUPU_TXT_SYNC_STARTING;
    case PH_OPEN: return s_linked ? PUPU_TXT_SYNC_LINKED : PUPU_TXT_SYNC_WAITING;
    case PH_CLOSING: return PUPU_TXT_SYNC_CLOSING;
    default: break;
    }
    switch (s_end) {
    case END_NO_PHONE: return PUPU_TXT_SYNC_NO_PHONE;
    case END_LOST: return PUPU_TXT_SYNC_LOST;
    case END_START_FAILED: return PUPU_TXT_SYNC_FAILED;
    case END_ERROR: return PUPU_TXT_SYNC_ERROR;
    default: return NULL;
    }
}

static void draw(uint32_t now_ms) {
    const pupu_model_t *m = s_env->model;
    if (s_waving && (int32_t)(now_ms - s_wave_until_ms) >= 0) s_waving = false;
    pupu_pet_view_play(pupu_ui_pet_anim(m, s_waving ? PUPU_POSE_WAVE : PUPU_POSE_IDLE), true, now_ms);
    pupu_pet_view_set_tint(pupu_tint_step(m->full, m->happy));
    pupu_pet_view_tick(now_ms);

    if (s_note && (int32_t)(now_ms - s_note_until_ms) >= 0) s_note = NULL;
    pupu_bubble_show(&s_bubble, s_note ? s_note : bubble_text());

    // 等手机来连时:右上角倒数,名字下面提示去手机上点"同步"
    const bool waiting = s_phase == PH_OPEN && !s_win.ever_connected;
    const unsigned secs = waiting ? pupu_sync_window_wait_left_s(&s_win, now_ms) : 0;
    if (secs != s_shown_secs) {
        s_shown_secs = secs;
        if (secs == 0) {
            lv_obj_add_flag(s_secs_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            snprintf(s_secs, sizeof(s_secs), PUPU_TXT_SYNC_SECS, secs);
            lv_label_set_text(s_secs_label, s_secs);
            lv_obj_align(s_secs_label, LV_ALIGN_TOP_RIGHT, -22, 10);
            lv_obj_remove_flag(s_secs_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool open = s_phase == PH_STARTING || s_phase == PH_OPEN;
    if (open == lv_obj_has_flag(s_name_label, LV_OBJ_FLAG_HIDDEN)) {
        if (open) lv_obj_remove_flag(s_name_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_name_label, LV_OBJ_FLAG_HIDDEN);
    }
    const bool hint = s_phase == PH_OPEN && !s_linked && s_end != END_DONE;
    if (hint == lv_obj_has_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN)) {
        if (hint) lv_obj_remove_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_hint_label, LV_OBJ_FLAG_HIDDEN);
    }
}

pupu_ui_req_t pupu_ui_sync_update(uint32_t now_ms) {
    const pupu_ui_req_t req = handle_events(now_ms);
    step(now_ms);
    draw(now_ms);
    if (s_phase == PH_CLOSED && s_leave) return PUPU_UI_GO_HOME;
    return req;
}

bool pupu_ui_sync_busy(void) {
    // 蓝牙关不掉也算忙:不能熄屏、更不能带着射频深睡
    return s_phase != PH_CLOSED || s_waving || s_stuck;
}

uint64_t pupu_ui_sync_away_pet_s(void) {
    return s_away_pet_s;
}

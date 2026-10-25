// main/pupu_ui_gate.c —— 小游戏「羊过门」的页面。规则全在 gate_model 里,这里只负责
// 计时、画面、音效和结算。
//
// - 流程:选难度(↑↓ 普通 / 学霸,OK 松开开始)→ 3-2-1 倒数 → 6 波 → 狼群滑进来 → 结果牌。
// - 计时:app 主循环在这一页改成每 20 ms 一次。每次按真实时间累加,每满 20 ms 推进一步,
//   一次最多补 5 步,多出来的丢掉。开局、继续时重新对准计时基准。
// - 对象:进页面时一次性建好 3 张卡(矩形 + 2 个标签)、倒计时条、顶栏 2 个标签、噗噗
//   (48×48 的 I4 小画布)、难度选择条、倒数圆牌、反馈标签、狼群图标 + 标签、暂停牌、结果牌,
//   之后只藏显和挪位置,不创建、不删除对象。背景是纯色。
// - 结算:第一次收到"结束"事件时调用一次 pupu_model_apply_gate_result(),然后显示结果;
//   约 1 秒后、等音效放完再存档(返回 PUPU_UI_CHANGED)。长按退出不结算,直接回玩耍页。
// - 串口日志记下帧率、换道延迟和 LVGL 内存池,用来对照验收指标(和接星星同一套)。
#include "pupu_ui.h"

#include "gate_model.h"
#include "pupu_config.h"
#include "pupu_sfx.h"
#include "pupu_sprite_math.h"
#include "pupu_text.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "pupu_gate";

#define MAX_STEPS 5            // 一次最多补几步
#define RESULT_LOCK_MS 600     // 结果出来后这么久内不响应按键,免得游戏里的按键漏进来
#define POSE_MS 400            // 选到最佳 / 最差时噗噗的表情停留多久
#define FEEDBACK_MS 900        // "最佳！/ 还行 / 差了"显示多久
#define WOLF_SLIDE_TICKS 50    // 狼群滑进来用几步(1 秒),之后停着比大小
#define BADGE_D 56             // 倒数圆牌的直径
#define CARD_RADIUS 8
#define CARD_BORDER 2
#define CARD_LINE1_Y 9         // 两行文字时第一行离卡顶多少
#define CARD_LINE2_Y 33
#define COLOR_GOOD 0x2E8B57    // 选到最佳时卡片边框闪一下绿

static const char *const k_levels[] = {PUPU_TXT_LEVEL_EASY, PUPU_TXT_LEVEL_HARD};
_Static_assert(GATE_EASY == 0 && GATE_HARD == 1, "k_levels is indexed by gate_level_t");

static gate_model_t s_game;
static gate_level_t s_level = GATE_EASY;  // 上次选的档位,下次进页面默认选它
static bool s_settled;
static uint32_t s_last_ms;
static uint32_t s_acc_ms;
static uint32_t s_result_ms;
static unsigned s_shown_count;

// 卡片
static lv_obj_t *s_cards[GATE_LANES];
static lv_obj_t *s_card_l1[GATE_LANES];
static lv_obj_t *s_card_l2[GATE_LANES];
static int s_card_x[GATE_LANES];
static bool s_card_shown[GATE_LANES];

// 顶栏和倒计时条
static lv_obj_t *s_sheep_label;
static lv_obj_t *s_wave_label;
static unsigned s_shown_sheep;
static unsigned s_shown_wave;
static lv_obj_t *s_bar_track;
static lv_obj_t *s_bar_fill;
static int s_shown_bar_w;

// 选难度
static pupu_picker_t s_picker;
static lv_obj_t *s_level_hint;

// 倒数、暂停、反馈、狼群、结果牌
static lv_obj_t *s_badge;
static lv_obj_t *s_badge_num;
static lv_obj_t *s_pause;
static lv_obj_t *s_feedback;
static uint32_t s_feedback_until_ms;
static lv_image_dsc_t s_wolf_dsc;
static lv_obj_t *s_wolf;
static lv_obj_t *s_wolf_label;
static int s_wolf_x;
static lv_obj_t *s_result;
static lv_obj_t *s_result_text;
static char s_result_buf[200];

// 噗噗的 1 倍小画布:16 色调色板 + 48 行 × 24 字节,放在 LVGL 内存池外面
static LV_ATTRIBUTE_MEM_ALIGN uint8_t s_pet_buf[PUPU_PALETTE_SIZE * 4 + PUPU_SPRITE_FRAME_BYTES];
static lv_draw_buf_t s_pet_draw;
static lv_obj_t *s_pet;
static int s_pet_lane;
static int s_pet_frame;
static pupu_pose_t s_pose;
static uint32_t s_pose_until_ms;

// 验收用的测量(只在游戏中计数)
static bool s_refr_hooked;
static bool s_measure;
static uint32_t s_frames;           // 这一秒画了几帧
static uint32_t s_frames_total;
static uint32_t s_fps_min;
static uint32_t s_fps_second_ms;
static int64_t s_render_start_us;
static uint32_t s_render_max_us;
static uint8_t s_render_max_state;  // 最慢的那一帧画的时候游戏在哪个状态(gate_state_t)
static bool s_lane_pending;         // 换道以后还没画出来
static uint32_t s_lane_key_ms;      // 那次换道的按键时间
static uint32_t s_lane_lat_max_ms;

static void render_cards(void);
static void render_hud(void);

static lv_color32_t to_lv32(uint32_t argb) {
    return lv_color32_make((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                           (uint8_t)(argb >> 24));
}

static void refr_cb(lv_event_t *e) {
    if (!s_measure) return;
    const lv_event_code_t code = lv_event_get_code(e);
    const int64_t now_us = esp_timer_get_time();
    if (code == LV_EVENT_RENDER_START) {
        s_render_start_us = now_us;
        return;
    }
    // LV_EVENT_RENDER_READY:真的画了一帧
    s_frames++;
    s_frames_total++;
    const uint32_t took = (uint32_t)(now_us - s_render_start_us);
    if (took > s_render_max_us) {
        s_render_max_us = took;
        s_render_max_state = s_game.state;
    }
    if (s_lane_pending) {
        s_lane_pending = false;
        const uint32_t lat = (uint32_t)(now_us / 1000) - s_lane_key_ms;
        if (lat > s_lane_lat_max_ms) s_lane_lat_max_ms = lat;
    }
}

static void measure_second(uint32_t now_ms) {
    if (now_ms - s_fps_second_ms < 1000) return;
    if (s_frames < s_fps_min) s_fps_min = s_frames;
    s_frames = 0;
    s_fps_second_ms = now_ms;
}

// why 只进串口日志;用英文,字表检查分不出它不上屏。
static void log_measure(const char *why) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "%s:共画 %" PRIu32 " 帧,动画中每秒最少 %" PRIu32 " 帧,一帧最长 %" PRIu32
                  " ms(状态 %u),换道到画出最长 %" PRIu32 " ms;LVGL 池剩余 %" PRIu32 " B 最大空块 %" PRIu32
                  " B 峰值 %" PRIu32 " B",
             why, s_frames_total, s_fps_min == UINT32_MAX ? 0 : s_fps_min, s_render_max_us / 1000,
             (unsigned)s_render_max_state, s_lane_lat_max_ms, (uint32_t)mon.free_size,
             (uint32_t)mon.free_biggest_size, (uint32_t)mon.max_used);
}

static void pet_show(const pupu_model_t *m, pupu_pose_t pose) {
    const pupu_anim_t anim = pupu_ui_pet_anim(m, pose);
    // 开心、躲开用第 2 帧(跳起来、躲得更远),其他用第 1 帧
    const int frame = pupu_anims[anim].first + (pose == PUPU_POSE_IDLE ? 0 : 1);
    if (frame == s_pet_frame) return;
    s_pet_frame = frame;
    memcpy(lv_draw_buf_goto_xy(&s_pet_draw, 0, 0), pupu_frames[frame], PUPU_SPRITE_FRAME_BYTES);
    lv_obj_invalidate(s_pet);
}

static void pet_tint(const pupu_model_t *m) {
    uint32_t palette[PUPU_PALETTE_SIZE];
    pupu_tint_palette(pupu_asset_palette, pupu_tint_step(m->full, m->happy), palette);
    for (int i = 0; i < PUPU_PALETTE_SIZE; i++) {
        lv_draw_buf_set_palette(&s_pet_draw, (uint8_t)i, to_lv32(palette[i]));
    }
}

static void pet_move(void) {
    if (s_game.lane == s_pet_lane) return;
    s_pet_lane = s_game.lane;
    lv_obj_set_pos(s_pet, GATE_PET_X, gate_pet_y(s_pet_lane));
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *l = pupu_ui_label(parent, text, color);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *framed_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius) {
    lv_obj_t *o = pupu_ui_rect(parent, x, y, w, h, PUPU_COLOR_PAPER, radius);
    lv_obj_set_style_border_color(o, lv_color_hex(PUPU_COLOR_INK), 0);
    lv_obj_set_style_border_width(o, CARD_BORDER, 0);
    return o;
}

static void show(lv_obj_t *o, bool on) {
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void picker_show(bool on) {
    show(s_picker.left, on);
    show(s_picker.pill, on);
    show(s_picker.right, on);
    show(s_level_hint, on);
}

lv_obj_t *pupu_ui_gate_create(const pupu_model_t *m, uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();
    const uint32_t seed = esp_random();
    gate_init(&s_game, seed, s_level);
    s_settled = false;
    s_last_ms = now_ms;
    s_acc_ms = 0;
    s_shown_count = 0;

    // 跑道之间的分隔线
    for (int i = 1; i < GATE_LANES; i++) {
        pupu_ui_rect(scr, 16, GATE_LANE_TOP + GATE_LANE_H * i - 1, GATE_SCREEN_W - 32, 2,
                     PUPU_COLOR_SOFT, 1);
    }

    // 顶栏:左边"羊 N",右边"第 k/6 波";下面倒计时条
    s_sheep_label = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    s_shown_sheep = UINT32_MAX;
    s_wave_label = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    s_shown_wave = UINT32_MAX;
    show(s_wave_label, false);
    s_bar_track = pupu_ui_rect(scr, GATE_BAR_X, GATE_BAR_Y, GATE_BAR_W, GATE_BAR_H, PUPU_COLOR_SOFT,
                               GATE_BAR_H / 2);
    s_bar_fill = pupu_ui_rect(s_bar_track, 0, 0, GATE_BAR_W, GATE_BAR_H, PUPU_COLOR_ACCENT, GATE_BAR_H / 2);
    s_shown_bar_w = GATE_BAR_W;
    show(s_bar_track, false);

    // 3 张卡,先藏起来
    for (int lane = 0; lane < GATE_LANES; lane++) {
        s_cards[lane] = framed_rect(scr, GATE_CARD_X, gate_card_y(lane), GATE_CARD_W, GATE_CARD_H, CARD_RADIUS);
        s_card_l1[lane] = centered_label(s_cards[lane], "", PUPU_COLOR_INK);
        s_card_l2[lane] = centered_label(s_cards[lane], "", PUPU_COLOR_INK);
        show(s_cards[lane], false);
        s_card_shown[lane] = false;
        s_card_x[lane] = GATE_CARD_X;
    }

    // 噗噗
    lv_draw_buf_init(&s_pet_draw, PUPU_SPRITE_W, PUPU_SPRITE_W, LV_COLOR_FORMAT_I4,
                     PUPU_SPRITE_ROW_BYTES, s_pet_buf, sizeof(s_pet_buf));
    pet_tint(m);
    s_pet = lv_canvas_create(scr);
    lv_canvas_set_draw_buf(s_pet, &s_pet_draw);
    s_pet_lane = -1;
    s_pet_frame = -1;
    s_pose = PUPU_POSE_IDLE;
    pet_move();
    pet_show(m, PUPU_POSE_IDLE);

    // 选难度:提示在第 0 条跑道,选择条在底部
    s_level_hint = centered_label(scr, PUPU_TXT_PICK_LEVEL, PUPU_COLOR_INK);
    lv_obj_align(s_level_hint, LV_ALIGN_TOP_MID, 0, gate_lane_center_y(0) - 10);
    pupu_picker_create(&s_picker, scr, k_levels, 2, (int)s_level, false);

    // 倒数圆牌(倒数时卡片还没出现,可以压在跑道上)
    s_badge = pupu_ui_rect(scr, (GATE_SCREEN_W - BADGE_D) / 2, gate_lane_center_y(1) - BADGE_D / 2,
                           BADGE_D, BADGE_D, PUPU_COLOR_INK, BADGE_D / 2);
    s_badge_num = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_num, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_badge_num, lv_color_hex(PUPU_COLOR_PAPER), 0);
    lv_label_set_text(s_badge_num, "3");
    lv_obj_center(s_badge_num);
    show(s_badge, false);

    // 反馈标签("最佳！"等),带纸色底,显示在噗噗所在跑道的上沿
    s_feedback = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    lv_obj_set_style_bg_color(s_feedback, lv_color_hex(PUPU_COLOR_PAPER), 0);
    lv_obj_set_style_bg_opa(s_feedback, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_feedback, 8, 0);
    lv_obj_set_style_pad_hor(s_feedback, 8, 0);
    lv_obj_set_style_pad_ver(s_feedback, 2, 0);
    show(s_feedback, false);

    // 狼群:图标从右边滑进中间跑道,标签在第 0 条跑道
    memset(&s_wolf_dsc, 0, sizeof(s_wolf_dsc));
    s_wolf_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_wolf_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    s_wolf_dsc.header.w = PUPU_ICON_WOLF_W;
    s_wolf_dsc.header.h = PUPU_ICON_WOLF_W;
    s_wolf_dsc.header.stride = PUPU_ICON_WOLF_W * 2;
    s_wolf_dsc.data_size = PUPU_ICON_WOLF_BYTES;
    s_wolf_dsc.data = pupu_icon_wolf;
    _Static_assert(PUPU_ICON_WOLF_W == GATE_WOLF_W, "gate_layout and the wolf icon agree on the size");
    s_wolf = lv_image_create(scr);
    lv_image_set_src(s_wolf, &s_wolf_dsc);
    s_wolf_x = GATE_SCREEN_W;
    lv_obj_set_pos(s_wolf, s_wolf_x, gate_lane_center_y(1) - GATE_WOLF_W / 2);
    show(s_wolf, false);
    s_wolf_label = centered_label(scr, "", PUPU_COLOR_INK);
    show(s_wolf_label, false);

    // 暂停牌
    s_pause = framed_rect(scr, 20, gate_lane_center_y(1) - 34, GATE_SCREEN_W - 40, 68, 12);
    lv_obj_t *p = centered_label(s_pause, PUPU_TXT_PAUSED "\n" PUPU_TXT_PAUSE_HINT, PUPU_COLOR_INK);
    lv_obj_center(p);
    show(s_pause, false);

    // 结果牌
    s_result = framed_rect(scr, 16, 70, GATE_SCREEN_W - 32, 190, 14);
    s_result_text = centered_label(s_result, "", PUPU_COLOR_INK);
    lv_obj_set_style_text_line_space(s_result_text, 6, 0);
    show(s_result, false);

    // 测量
    if (!s_refr_hooked) {
        lv_display_t *disp = lv_display_get_default();
        lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_READY, NULL);
        s_refr_hooked = true;
    }
    s_measure = false;
    s_frames = s_frames_total = 0;
    s_fps_min = UINT32_MAX;
    s_render_max_us = 0;
    s_render_max_state = 0;
    s_lane_pending = false;
    s_lane_lat_max_ms = 0;

    ESP_LOGI(TAG, "进入羊过门,种子 0x%08" PRIx32 ",默认档位 %d", seed, (int)s_level);
    render_hud();
    return scr;
}

static void append(char *buf, size_t cap, const char *name, int value) {
    const size_t n = strlen(buf);
    if (n < cap) snprintf(buf + n, cap - n, "%s%s %+d", n > 0 && buf[n - 1] != '\n' ? "  " : "",
                          name, value);
}

static void newline(char *buf, size_t cap) {
    const size_t n = strlen(buf);
    if (n + 1 < cap) {
        buf[n] = '\n';
        buf[n + 1] = '\0';
    }
}

static unsigned wolves_shown(void) {
    const uint32_t w = gate_wolves(&s_game);
    return w > GATE_WOLVES_SHOW_MAX ? GATE_WOLVES_SHOW_MAX : (unsigned)w;
}

// 结果牌:羊和狼的数量、赢没赢、实际得到了什么(已经过上限截断)。
static void show_result(const pupu_model_t *m, const pupu_outcome_t *o) {
    char *b = s_result_buf;
    const size_t cap = sizeof(s_result_buf);
    snprintf(b, cap, PUPU_TXT_SHEEP "  " PUPU_TXT_WOLF_COUNT "\n", (unsigned)s_game.sheep, wolves_shown());
    const bool won = o->result == PUPU_OK;
    snprintf(b + strlen(b), cap - strlen(b), "%s%s\n", won ? PUPU_TXT_GATE_WIN : PUPU_TXT_GATE_LOSE,
             o->grew ? PUPU_TXT_GREW : "");
    if (won) {
        append(b, cap, PUPU_TXT_HAPPY, o->happy);
        if (o->bond != 0) append(b, cap, PUPU_TXT_BOND, o->bond);
        newline(b, cap);
        if (o->candy > 0) {
            append(b, cap, PUPU_TXT_CANDY, o->candy);
        } else {
            // 没给星星糖:要么库存满 99,要么今天已经从小游戏领了 3 颗
            const size_t n = strlen(b);
            snprintf(b + n, cap - n, "%s",
                     m->candy >= PUPU_CANDY_MAX ? PUPU_TXT_CANDY_STOCK : PUPU_TXT_CANDY_DONE);
        }
        newline(b, cap);
    }
    append(b, cap, PUPU_TXT_FULL, o->full);
    const size_t n = strlen(b);
    snprintf(b + n, cap - n, "\n\n%s", PUPU_TXT_RESULT_HINT);
    lv_label_set_text(s_result_text, b);
    lv_obj_center(s_result_text);
    show(s_pause, false);
    show(s_result, true);
}

static pupu_ui_req_t settle(pupu_model_t *m, uint32_t now_ms) {
    if (s_settled) return PUPU_UI_NONE;
    s_settled = true;
    s_measure = false;
    const gate_result_t r = gate_result(&s_game);
    const pupu_outcome_t o = pupu_model_apply_gate_result(m, r.completed, r.won);
    char picks[GATE_WAVES + 1];
    for (int i = 0; i < GATE_WAVES; i++) picks[i] = (char)('0' + s_game.picks[i]);
    picks[GATE_WAVES] = '\0';
    ESP_LOGI(TAG, "结束:羊 %u 狼 %" PRIu32 " R=%u‰ 每波选的名次 %s(0 最佳)%s 结果 %d 变化 饱腹%+d "
                  "开心%+d 亲密+%u 星星糖%+d%s → 饱腹=%d 开心=%d 亲密=%d 星星糖=%d",
             (unsigned)r.sheep, r.wolves, (unsigned)r.r_permille, picks, r.won ? " win" : " lose",
             (int)o.result, o.full, o.happy, (unsigned)o.bond, o.candy, o.grew ? " 长大了" : "",
             m->full, m->happy, m->bond, m->candy);
    log_measure("round end");
    pupu_sfx_play(o.result == PUPU_OK ? PUPU_SFX_WIN : PUPU_SFX_LOSE);
    if (o.grew) pupu_sfx_play(PUPU_SFX_GROW);
    show_result(m, &o);
    s_result_ms = now_ms;
    return PUPU_UI_CHANGED;
}

pupu_ui_req_t pupu_ui_gate_action(pupu_action_t act, pupu_model_t *m, uint32_t key_ms,
                                  uint32_t now_ms) {
    if (s_game.state == GATE_FINISHED) {
        if (now_ms - s_result_ms < RESULT_LOCK_MS) return PUPU_UI_NONE;
        return act == PUPU_ACT_CONFIRM || act == PUPU_ACT_BACK ? PUPU_UI_GO_PLAY : PUPU_UI_NONE;
    }
    gate_input_t in;
    switch (act) {
    case PUPU_ACT_PREV: in = GATE_IN_UP; break;
    case PUPU_ACT_NEXT: in = GATE_IN_DOWN; break;
    case PUPU_ACT_OK_DOWN: in = GATE_IN_OK_PRESS; break;
    case PUPU_ACT_OK_UP: in = GATE_IN_OK_RELEASE; break;
    case PUPU_ACT_BACK: in = GATE_IN_OK_LONG; break;
    default: return PUPU_UI_NONE;
    }
    const uint32_t ev = gate_input(&s_game, in);
    if (ev & GATE_EV_LEVEL) {
        s_level = (gate_level_t)s_game.level;
        pupu_picker_action(&s_picker, act);  // 选择条和模型一起动:↑ 普通、↓ 学霸
    }
    if (ev & GATE_EV_COUNTDOWN) {
        ESP_LOGI(TAG, "开局,档位 %s", s_game.level == GATE_HARD ? "hard" : "easy");
        picker_show(false);
        show(s_badge, true);
        pupu_sfx_play(PUPU_SFX_BEEP);
        s_last_ms = now_ms;
        s_acc_ms = 0;
    }
    if (ev & GATE_EV_LANE) {
        pet_move();  // 马上挪,不等下一步
        if (s_measure) {
            s_lane_pending = true;
            s_lane_key_ms = key_ms;
        }
    }
    if (ev & GATE_EV_PAUSED) {
        ESP_LOGI(TAG, "暂停");
        show(s_pause, true);
        s_measure = false;  // 暂停时画面不动,不算帧率
        s_lane_pending = false;
    }
    if (ev & GATE_EV_RESUMED) {
        ESP_LOGI(TAG, "继续");
        show(s_pause, false);
        s_last_ms = now_ms;  // 重新对准计时基准,暂停的时间不补
        s_acc_ms = 0;
        s_measure = true;
        s_frames = 0;
        s_fps_second_ms = now_ms;
    }
    if (ev & GATE_EV_EXIT) {
        // 中途退出不算一局:不结算(pupu_model_apply_gate_result 对没完成的一局什么都不做)
        s_measure = false;
        ESP_LOGI(TAG, "长按退出,这一局不算(第 %u 波,羊 %u)", (unsigned)s_game.wave,
                 (unsigned)s_game.sheep);
        log_measure("exit");
        return PUPU_UI_GO_PLAY;
    }
    return PUPU_UI_NONE;
}

// 新一波:把三张卡的文字写上,摆回卡位。
static void place_wave(void) {
    for (int lane = 0; lane < GATE_LANES; lane++) {
        const gate_card_t *c = gate_card(&s_game, lane);
        lv_label_set_text(s_card_l1[lane], c->line1);
        lv_label_set_text(s_card_l2[lane], c->line2);
        if (c->line2[0] != '\0') {
            lv_obj_align(s_card_l1[lane], LV_ALIGN_TOP_MID, 0, CARD_LINE1_Y);
            lv_obj_align(s_card_l2[lane], LV_ALIGN_TOP_MID, 0, CARD_LINE2_Y);
            show(s_card_l2[lane], true);
        } else {
            lv_obj_center(s_card_l1[lane]);
            show(s_card_l2[lane], false);
        }
        lv_obj_set_style_border_color(s_cards[lane], lv_color_hex(PUPU_COLOR_INK), 0);
        s_card_x[lane] = GATE_CARD_X;
        lv_obj_set_x(s_cards[lane], GATE_CARD_X);
        show(s_cards[lane], true);
        s_card_shown[lane] = true;
    }
}

static void render_cards(void) {
    if (s_game.state == GATE_APPLYING || (s_game.state == GATE_PAUSED && s_game.paused_from == GATE_APPLYING)) {
        // 选中的卡片滑向噗噗,另外两张消失
        for (int lane = 0; lane < GATE_LANES; lane++) {
            if (lane != s_game.chosen_lane) {
                if (s_card_shown[lane]) {
                    show(s_cards[lane], false);
                    s_card_shown[lane] = false;
                }
                continue;
            }
            const int x = gate_card_slide_x(s_game.apply_ticks, gate_tuning.apply_ticks);
            if (x != s_card_x[lane]) {
                s_card_x[lane] = x;
                lv_obj_set_x(s_cards[lane], x);
            }
        }
    }
}

static void feedback(const char *text, int lane, uint32_t now_ms) {
    lv_label_set_text(s_feedback, text);
    lv_obj_set_pos(s_feedback, GATE_PET_X, gate_lane_top(lane) + 1);
    show(s_feedback, true);
    s_feedback_until_ms = now_ms + FEEDBACK_MS;
}

static void render_hud(void) {
    if (s_game.sheep != s_shown_sheep) {
        s_shown_sheep = s_game.sheep;
        lv_label_set_text_fmt(s_sheep_label, PUPU_TXT_SHEEP, s_shown_sheep);
        lv_obj_set_pos(s_sheep_label, 24, 12);
    }
    if (s_game.wave > 0 && s_game.wave != s_shown_wave) {
        s_shown_wave = s_game.wave;
        lv_label_set_text_fmt(s_wave_label, PUPU_TXT_WAVE, s_shown_wave);
        lv_obj_align(s_wave_label, LV_ALIGN_TOP_RIGHT, -24, 12);
        show(s_wave_label, true);
    }
    // 倒计时条:波进行中从满到空;卡片滑动时是空的
    if (s_game.wave > 0 && s_game.state != GATE_WOLVES && s_game.state != GATE_FINISHED) {
        const unsigned left = gate_wave_ms_left(&s_game);
        const unsigned total = (unsigned)s_game.wave_len * GATE_TICK_MS;
        const int w = total > 0 ? (int)((uint32_t)GATE_BAR_W * left / total) : 0;
        if (w != s_shown_bar_w) {
            s_shown_bar_w = w;
            lv_obj_set_width(s_bar_fill, w);
        }
    }
    const unsigned count = gate_countdown_number(&s_game);
    if (count != s_shown_count) {
        if (count > 0 && s_shown_count != 0) pupu_sfx_play(PUPU_SFX_BEEP);
        s_shown_count = count;
        if (count > 0) {
            lv_label_set_text_fmt(s_badge_num, "%u", count);
            lv_obj_center(s_badge_num);
        }
    }
}

pupu_ui_req_t pupu_ui_gate_update(pupu_model_t *m, uint32_t now_ms) {
    uint32_t ev = 0;
    if (gate_active(&s_game)) {
        s_acc_ms += now_ms - s_last_ms;
        int steps = 0;
        while (s_acc_ms >= GATE_TICK_MS && steps < MAX_STEPS && gate_active(&s_game)) {
            ev |= gate_tick(&s_game);
            s_acc_ms -= GATE_TICK_MS;
            steps++;
        }
        if (s_acc_ms >= GATE_TICK_MS) s_acc_ms = 0;  // 落后太多:丢掉,不追
    }
    s_last_ms = now_ms;

    if (ev & GATE_EV_START) {
        show(s_badge, false);
        show(s_bar_track, true);
        s_measure = true;
        s_fps_second_ms = now_ms;
        s_frames = 0;
    }
    if (ev & GATE_EV_WAVE) place_wave();
    if (ev & GATE_EV_APPLYING) {
        // 选到最佳的卡片滑动时边框闪绿
        const bool best = gate_card(&s_game, s_game.chosen_lane)->rank == 0;
        lv_obj_set_style_border_color(s_cards[s_game.chosen_lane],
                                      lv_color_hex(best ? COLOR_GOOD : PUPU_COLOR_INK), 0);
    }
    if (ev & GATE_EV_PICK_BEST) {
        pupu_sfx_play(PUPU_SFX_CARD_GOOD);
        feedback(PUPU_TXT_PICK_BEST, s_game.lane, now_ms);
        s_pose = PUPU_POSE_HAPPY;
        s_pose_until_ms = now_ms + POSE_MS;
    }
    if (ev & GATE_EV_PICK_SECOND) feedback(PUPU_TXT_PICK_SECOND, s_game.lane, now_ms);
    if (ev & GATE_EV_PICK_THIRD) {
        pupu_sfx_play(PUPU_SFX_CARD_BAD);
        feedback(PUPU_TXT_PICK_THIRD, s_game.lane, now_ms);
        s_pose = PUPU_POSE_DODGE;
        s_pose_until_ms = now_ms + POSE_MS;
    }
    if (ev & GATE_EV_WOLVES) {
        for (int lane = 0; lane < GATE_LANES; lane++) {
            show(s_cards[lane], false);
            s_card_shown[lane] = false;
        }
        show(s_bar_track, false);
        pupu_sfx_play(PUPU_SFX_HOWL);
        lv_label_set_text_fmt(s_wolf_label, PUPU_TXT_WOLVES, wolves_shown());
        lv_obj_align(s_wolf_label, LV_ALIGN_TOP_MID, 0, gate_lane_center_y(0) - 10);
        show(s_wolf_label, true);
        show(s_wolf, true);
        ESP_LOGI(TAG, "第 6 波结束:羊 %u,狼 %" PRIu32 ",R=%u‰", (unsigned)s_game.sheep,
                 gate_wolves(&s_game), (unsigned)s_game.r_permille);
    }
    if (s_game.state == GATE_WOLVES) {
        const int t = s_game.wolves_ticks < WOLF_SLIDE_TICKS ? s_game.wolves_ticks : WOLF_SLIDE_TICKS;
        const int x = gate_wolf_slide_x(t, WOLF_SLIDE_TICKS);
        if (x != s_wolf_x) {
            s_wolf_x = x;
            lv_obj_set_x(s_wolf, x);
        }
    }
    if (s_pose != PUPU_POSE_IDLE && (int32_t)(now_ms - s_pose_until_ms) >= 0) s_pose = PUPU_POSE_IDLE;
    if (!lv_obj_has_flag(s_feedback, LV_OBJ_FLAG_HIDDEN) && (int32_t)(now_ms - s_feedback_until_ms) >= 0) {
        show(s_feedback, false);
    }

    render_cards();
    render_hud();
    pet_move();
    pet_show(m, s_pose);
    // 只在画面持续在动的时候量帧率:波进行中(倒计时条)、卡片滑动、狼群滑入。
    // 狼群停下来比大小的那 1 秒画面是静止的,LVGL 不重画,算进去会把"每秒最少帧"拉成 0。
    const bool animating = gate_active(&s_game) &&
                           (s_game.state != GATE_WOLVES || s_game.wolves_ticks < WOLF_SLIDE_TICKS);
    if (s_measure && animating) {
        measure_second(now_ms);
    } else if (s_measure) {
        s_frames = 0;  // 静止阶段不计,恢复动起来时重新数这一秒
        s_fps_second_ms = now_ms;
    }

    if (ev & GATE_EV_FINISHED) return settle(m, now_ms);
    return PUPU_UI_NONE;
}

bool pupu_ui_gate_busy(void) {
    return gate_active(&s_game);
}

bool pupu_ui_gate_in_round(void) {
    return gate_in_round(&s_game);
}

void pupu_ui_gate_leave(void) {
    if (gate_in_round(&s_game)) {
        ESP_LOGI(TAG, "离开羊过门,这一局不算");
        gate_input(&s_game, GATE_IN_OK_LONG);  // 作废
    }
    s_measure = false;
    s_lane_pending = false;
}

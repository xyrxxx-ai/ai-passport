// main/pupu_ui_catch.c —— 小游戏「接星星」的页面。规则全在 catch_model 里,这里只负责
// 计时、画面、音效和结算。
//
// - 计时:app 主循环在这一页改成每 20 ms 一次。每次按真实时间累加,每满 20 ms 推进一步,
//   一次最多补 5 步,多出来的丢掉。开局、继续时重新对准计时基准。
// - 对象:进页面时一次性建好 8 个物体图标、噗噗(48×48 的 I4 小画布)、顶栏和提示,
//   游戏中只藏起、显示和挪位置,不创建、不删除对象。背景是纯色。
// - 结算:第一次收到"结束"事件时调用一次 pupu_model_apply_catch_result(),然后显示结果;
//   约 1 秒后、等音效放完再存档(返回 PUPU_UI_CHANGED)。长按退出不结算,直接回玩耍页。
// - 串口日志记下帧率、换道延迟和 LVGL 内存池,用来对照验收指标。
#include "pupu_ui.h"

#include "catch_model.h"
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

static const char *TAG = "pupu_catch";

#define MAX_STEPS 5            // 一次最多补几步
#define RESULT_LOCK_MS 600     // 结果出来后这么久内不响应按键,免得游戏里的按键漏进来
#define POSE_MS 300            // 接到星星、碰到乌云时噗噗的表情停留多久
#define SCORE_X 162            // 顶栏星星图标的位置
#define BADGE_D 56             // 倒数圆牌的直径

static catch_model_t s_game;
static bool s_settled;
static uint32_t s_last_ms;
static uint32_t s_acc_ms;
static uint32_t s_result_ms;
static unsigned s_shown_count;

static lv_image_dsc_t s_star_dsc;
static lv_image_dsc_t s_cloud_dsc;
static lv_obj_t *s_items[CATCH_MAX_ITEMS];
static uint8_t s_item_kind[CATCH_MAX_ITEMS];  // 正在显示的种类,NONE 表示藏着
static int s_item_x[CATCH_MAX_ITEMS];
static lv_obj_t *s_secs;
static lv_obj_t *s_score;
static unsigned s_shown_secs;
static unsigned s_shown_score;
static lv_obj_t *s_badge;
static lv_obj_t *s_badge_num;
static lv_obj_t *s_hint;
static lv_obj_t *s_pause;
static lv_obj_t *s_result;
static lv_obj_t *s_result_text;
static char s_result_buf[160];

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
static bool s_lane_pending;         // 换道以后还没画出来
static uint32_t s_lane_key_ms;      // 那次换道的按键时间
static uint32_t s_lane_lat_max_ms;

static void render_items(void);
static void render_hud(void);

static void icon_dsc(lv_image_dsc_t *d, const uint8_t *data) {
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565A8;
    d->header.w = PUPU_ICON_W;
    d->header.h = PUPU_ICON_W;
    d->header.stride = PUPU_ICON_W * 2;  // RGB565 那一层的行宽
    d->data_size = PUPU_ICON_BYTES;
    d->data = data;
}

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
    if (took > s_render_max_us) s_render_max_us = took;
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
    ESP_LOGI(TAG, "%s:共画 %" PRIu32 " 帧,每秒最少 %" PRIu32 " 帧,一帧最长 %" PRIu32
                  " ms,换道到画出最长 %" PRIu32 " ms;LVGL 池剩余 %" PRIu32 " B 最大空块 %" PRIu32
                  " B 峰值 %" PRIu32 " B",
             why, s_frames_total, s_fps_min == UINT32_MAX ? 0 : s_fps_min, s_render_max_us / 1000,
             s_lane_lat_max_ms, (uint32_t)mon.free_size, (uint32_t)mon.free_biggest_size,
             (uint32_t)mon.max_used);
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
    lv_obj_set_pos(s_pet, CATCH_PET_X, catch_pet_y(s_pet_lane));
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *l = pupu_ui_label(parent, text, color);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t *pupu_ui_catch_create(const pupu_model_t *m, uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();
    const uint32_t seed = esp_random();
    catch_init(&s_game, seed);
    s_settled = false;
    s_last_ms = now_ms;
    s_acc_ms = 0;
    s_shown_count = 0;

    // 跑道之间的分隔线
    for (int i = 1; i < CATCH_LANES; i++) {
        pupu_ui_rect(scr, 16, CATCH_LANE_TOP + CATCH_LANE_H * i - 1, CATCH_SCREEN_W - 32, 2,
                     PUPU_COLOR_SOFT, 1);
    }

    // 顶栏:左边剩余秒数,右边星星数
    s_secs = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    s_shown_secs = UINT32_MAX;
    icon_dsc(&s_star_dsc, pupu_icon_star);
    icon_dsc(&s_cloud_dsc, pupu_icon_cloud);
    lv_obj_t *star = lv_image_create(scr);
    lv_image_set_src(star, &s_star_dsc);
    lv_obj_set_pos(star, SCORE_X, 10);
    s_score = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    s_shown_score = UINT32_MAX;

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

    // 8 个物体,先藏起来
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        s_items[i] = lv_image_create(scr);
        lv_image_set_src(s_items[i], &s_star_dsc);
        lv_obj_add_flag(s_items[i], LV_OBJ_FLAG_HIDDEN);
        s_item_kind[i] = CATCH_ITEM_NONE;
        s_item_x[i] = INT32_MIN;
    }

    // 倒数圆牌和玩法提示(倒数时物体不动,所以可以压在跑道上)
    s_badge = pupu_ui_rect(scr, (CATCH_SCREEN_W - BADGE_D) / 2, catch_lane_center_y(1) - BADGE_D / 2,
                           BADGE_D, BADGE_D, PUPU_COLOR_INK, BADGE_D / 2);
    s_badge_num = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_num, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_badge_num, lv_color_hex(PUPU_COLOR_PAPER), 0);
    lv_label_set_text(s_badge_num, "3");
    lv_obj_center(s_badge_num);
    s_hint = centered_label(scr, PUPU_TXT_CATCH_HINT1 "\n" PUPU_TXT_CATCH_HINT2, PUPU_COLOR_INK);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, catch_lane_center_y(2) - 18);

    // 暂停牌
    s_pause = pupu_ui_rect(scr, 20, catch_lane_center_y(1) - 34, CATCH_SCREEN_W - 40, 68,
                           PUPU_COLOR_PAPER, 12);
    lv_obj_set_style_border_color(s_pause, lv_color_hex(PUPU_COLOR_INK), 0);
    lv_obj_set_style_border_width(s_pause, 2, 0);
    lv_obj_t *p = centered_label(s_pause, PUPU_TXT_PAUSED "\n" PUPU_TXT_PAUSE_HINT, PUPU_COLOR_INK);
    lv_obj_center(p);
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);

    // 结果牌
    s_result = pupu_ui_rect(scr, 16, 70, CATCH_SCREEN_W - 32, 190, PUPU_COLOR_PAPER, 14);
    lv_obj_set_style_border_color(s_result, lv_color_hex(PUPU_COLOR_INK), 0);
    lv_obj_set_style_border_width(s_result, 2, 0);
    s_result_text = centered_label(s_result, "", PUPU_COLOR_INK);
    lv_obj_set_style_text_line_space(s_result_text, 6, 0);
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);

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
    s_lane_pending = false;
    s_lane_lat_max_ms = 0;

    ESP_LOGI(TAG, "开局,种子 0x%08" PRIx32, seed);
    pupu_sfx_play(PUPU_SFX_BEEP);
    render_items();
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

// 结果牌:接到几颗、赢没赢、实际得到了什么(已经过上限截断)。
static void show_result(const pupu_model_t *m, const pupu_outcome_t *o, unsigned stars) {
    char *b = s_result_buf;
    const size_t cap = sizeof(s_result_buf);
    snprintf(b, cap, PUPU_TXT_CATCH_GOT "\n", stars);
    const bool won = o->result == PUPU_OK;
    snprintf(b + strlen(b), cap - strlen(b), "%s%s\n", won ? PUPU_TXT_CATCH_WIN : PUPU_TXT_CATCH_LOSE,
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
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_result, LV_OBJ_FLAG_HIDDEN);
}

static pupu_ui_req_t settle(pupu_model_t *m, uint32_t now_ms) {
    if (s_settled) return PUPU_UI_NONE;
    s_settled = true;
    s_measure = false;
    const catch_result_t r = catch_result(&s_game);
    const pupu_outcome_t o = pupu_model_apply_catch_result(m, r.completed, r.stars);
    ESP_LOGI(TAG, "结束:%u 颗(一共接到 %u,碰乌云 %u,出了 %u 星 %u 云)结果 %d 变化 饱腹%+d 开心%+d "
                  "亲密+%u 星星糖%+d%s → 饱腹=%d 开心=%d 亲密=%d 星星糖=%d",
             r.stars, s_game.caught, s_game.clouds_hit, s_game.spawned_stars, s_game.spawned_clouds,
             (int)o.result, o.full, o.happy, (unsigned)o.bond, o.candy, o.grew ? " 长大了" : "",
             m->full, m->happy, m->bond, m->candy);
    log_measure("round end");
    pupu_sfx_play(o.result == PUPU_OK ? PUPU_SFX_WIN : PUPU_SFX_LOSE);
    if (o.grew) pupu_sfx_play(PUPU_SFX_GROW);
    show_result(m, &o, r.stars);
    s_result_ms = now_ms;
    return PUPU_UI_CHANGED;
}

pupu_ui_req_t pupu_ui_catch_action(pupu_action_t act, pupu_model_t *m, uint32_t key_ms,
                                   uint32_t now_ms) {
    if (s_game.state == CATCH_FINISHED) {
        if (now_ms - s_result_ms < RESULT_LOCK_MS) return PUPU_UI_NONE;
        return act == PUPU_ACT_CONFIRM || act == PUPU_ACT_BACK ? PUPU_UI_GO_PLAY : PUPU_UI_NONE;
    }
    catch_input_t in;
    switch (act) {
    case PUPU_ACT_PREV: in = CATCH_IN_UP; break;
    case PUPU_ACT_NEXT: in = CATCH_IN_DOWN; break;
    case PUPU_ACT_OK_DOWN: in = CATCH_IN_OK_PRESS; break;
    case PUPU_ACT_OK_UP: in = CATCH_IN_OK_RELEASE; break;
    case PUPU_ACT_BACK: in = CATCH_IN_OK_LONG; break;
    default: return PUPU_UI_NONE;
    }
    const uint32_t ev = catch_input(&s_game, in);
    if (ev & CATCH_EV_LANE) {
        pet_move();  // 马上挪,不等下一步
        if (s_measure) {
            s_lane_pending = true;
            s_lane_key_ms = key_ms;
        }
    }
    if (ev & CATCH_EV_PAUSED) {
        ESP_LOGI(TAG, "暂停");
        lv_obj_remove_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
        s_measure = false;  // 暂停时画面不动,不算帧率
        s_lane_pending = false;
    }
    if (ev & CATCH_EV_RESUMED) {
        ESP_LOGI(TAG, "继续");
        lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
        s_last_ms = now_ms;  // 重新对准计时基准,暂停的时间不补
        s_acc_ms = 0;
        s_measure = true;
        s_frames = 0;
        s_fps_second_ms = now_ms;
    }
    if (ev & CATCH_EV_EXIT) {
        // 中途退出不算一局:不结算(pupu_model_apply_catch_result 对没完成的一局什么都不做)
        s_measure = false;
        ESP_LOGI(TAG, "长按退出,这一局不算(当时 %u 颗)", s_game.stars);
        log_measure("exit");
        return PUPU_UI_GO_PLAY;
    }
    return PUPU_UI_NONE;
}

static void render_items(void) {
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        const catch_item_t *it = &s_game.items[i];
        if (it->kind == CATCH_ITEM_NONE) {
            if (s_item_kind[i] != CATCH_ITEM_NONE) {
                lv_obj_add_flag(s_items[i], LV_OBJ_FLAG_HIDDEN);
                s_item_kind[i] = CATCH_ITEM_NONE;
            }
            continue;
        }
        const int x = catch_item_x(it->cx_q4);
        if (s_item_kind[i] != it->kind) {
            lv_image_set_src(s_items[i], it->kind == CATCH_ITEM_STAR ? &s_star_dsc : &s_cloud_dsc);
            lv_obj_set_pos(s_items[i], x, catch_item_y(it->lane));
            lv_obj_remove_flag(s_items[i], LV_OBJ_FLAG_HIDDEN);
            s_item_kind[i] = it->kind;
        } else if (x != s_item_x[i]) {
            lv_obj_set_x(s_items[i], x);
        }
        s_item_x[i] = x;
    }
}

static void render_hud(void) {
    const unsigned secs = catch_seconds_left(&s_game);
    if (secs != s_shown_secs) {
        s_shown_secs = secs;
        lv_label_set_text_fmt(s_secs, PUPU_TXT_CATCH_SECS, secs);
        lv_obj_set_pos(s_secs, 24, 12);
    }
    if (s_game.stars != s_shown_score) {
        s_shown_score = s_game.stars;
        lv_label_set_text_fmt(s_score, PUPU_TXT_CATCH_SCORE, s_shown_score);
        lv_obj_set_pos(s_score, SCORE_X + PUPU_ICON_W + 6, 12);
    }
    const unsigned count = catch_countdown_number(&s_game);
    if (count != s_shown_count) {
        if (count > 0 && s_shown_count != 0) pupu_sfx_play(PUPU_SFX_BEEP);
        s_shown_count = count;
        if (count > 0) {
            lv_label_set_text_fmt(s_badge_num, "%u", count);
            lv_obj_center(s_badge_num);
        }
    }
}

pupu_ui_req_t pupu_ui_catch_update(pupu_model_t *m, uint32_t now_ms) {
    uint32_t ev = 0;
    if (catch_active(&s_game)) {
        s_acc_ms += now_ms - s_last_ms;
        int steps = 0;
        while (s_acc_ms >= CATCH_TICK_MS && steps < MAX_STEPS && catch_active(&s_game)) {
            ev |= catch_tick(&s_game);
            s_acc_ms -= CATCH_TICK_MS;
            steps++;
        }
        if (s_acc_ms >= CATCH_TICK_MS) s_acc_ms = 0;  // 落后太多:丢掉,不追
    }
    s_last_ms = now_ms;

    if (ev & CATCH_EV_START) {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        s_measure = true;
        s_fps_second_ms = now_ms;
        s_frames = 0;
    }
    if (ev & CATCH_EV_STAR) {
        pupu_sfx_play(PUPU_SFX_STAR);
        s_pose = PUPU_POSE_HAPPY;
        s_pose_until_ms = now_ms + POSE_MS;
    }
    if (ev & CATCH_EV_CLOUD) {
        pupu_sfx_play(PUPU_SFX_CLOUD);
        s_pose = PUPU_POSE_DODGE;
        s_pose_until_ms = now_ms + POSE_MS;
    }
    if (ev & CATCH_EV_FULL) ESP_LOGW(TAG, "8 个位置都满了,跳过一次出生");
    if (s_pose != PUPU_POSE_IDLE && (int32_t)(now_ms - s_pose_until_ms) >= 0) s_pose = PUPU_POSE_IDLE;

    render_items();
    render_hud();
    pet_move();
    pet_show(m, s_pose);
    if (s_measure) measure_second(now_ms);

    if (ev & CATCH_EV_FINISHED) return settle(m, now_ms);
    return PUPU_UI_NONE;
}

bool pupu_ui_catch_busy(void) {
    return catch_active(&s_game);
}

bool pupu_ui_catch_in_round(void) {
    return catch_in_round(&s_game);
}

void pupu_ui_catch_leave(void) {
    if (catch_in_round(&s_game)) {
        ESP_LOGI(TAG, "离开小游戏,这一局不算");
        catch_input(&s_game, CATCH_IN_OK_LONG);  // 作废
    }
    s_measure = false;
    s_lane_pending = false;
}

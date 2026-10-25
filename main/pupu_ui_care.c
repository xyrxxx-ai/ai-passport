// main/pupu_ui_care.c —— 喂食页和玩耍页:宠物在中间,上面是结果气泡,下面是选择条。
//
// 喂食:饭团 / 小饼干 / 星星糖,右上角显示"零花钱 N  糖 ×N";选中买不起的食物时胶囊里的字变淡。
// 玩耍:摸摸 / 打工 / 羊过门(小游戏页在 pupu_ui_gate.c)。
// ↑/↓ 选,OK 确认,长按 OK 回首页(产品说明第 7 节)。
// 确认以后噗噗先做一个反应(吃 2 秒、开心、躲开、摇头、打工、被砸晕),做完之前再按 OK 不响应;
// 气泡里写这次实际变了多少(已经过上限截断),比如"饱腹 +35  亲密 +3"。
//
// 摸摸和打工还有一层叠在宠物上方的 lv_image(小手、表格),主循环每 50 ms 挪一步:
// - 摸摸 1–3 次:小手从上方落到头上(6 步 × 10 px = 0.3 秒),"啪",停一下再抬起;噗噗落下时才跳。
// - 第 4 次:小手落下,"哼",噗噗皱眉侧头(之后的待机也皱眉,直到窗口结束)。
// - 第 5 次:小手落下被弹开,往右上飞走,三连降,噗噗生气跳开。
// - 第 6 次起:不出小手、不出气泡,只转身。
// - 打工 1、2 次:表格从上方掉下来,噗噗接住(帧里抱着表),"叮叮"。
// - 第 3、4、5 次:表格砸中头,"砰",噗噗倒地转圈。第 6 次起:摇头"做不动了"。
// 规则全在 pupu_model 里,这里只负责显示和放音效。
#include "pupu_ui.h"

#include "pupu_config.h"
#include "pupu_pet_view.h"
#include "pupu_sfx.h"
#include "pupu_sprite_math.h"
#include "pupu_text.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "pupu_care";

#define EAT_MS 2000     // 咀嚼动画放多久
#define REACT_MS 1200   // 开心、躲开、摇头、不耐烦、反抗放多久(从小手落地算起)
#define WORK_MS 1600    // 抱着表格 / 被砸晕放多久(从表格落地算起)
#define MSG_MS 2500     // 结果气泡显示多久
#define STEP_MS 50      // 叠层动画一步 = app 主循环一圈
#define FALL_STEPS 6    // 小手落下 0.3 秒
#define FALL_PX 10
#define HOLD_STEPS 2    // 拍在头上停 0.1 秒
#define FLY_STEPS 8     // 第 5 次:被弹开往右上飞 0.4 秒
#define FLY_DX 12
#define FLY_DY 10
#define SHEET_STEPS 8   // 表格从屏幕顶上掉下来 0.4 秒(每步的距离按落点算)

// 叠层图标的位置(左上角)。宠物画布 144×144 在 (PUPU_PET_X, PUPU_PET_Y),图标 48×48 居中。
#define OVERLAY_X (PUPU_PET_X + (PUPU_SPRITE_BIG_W - PUPU_ICON_HAND_W) / 2)
#define HAND_LAND_Y (PUPU_PET_Y - 18)     // 指尖落在蛋壳帽上
#define SHEET_CATCH_Y (PUPU_PET_Y + 66)   // 接住:落到身体前(帧里抱着表的位置),落地就藏起
#define SHEET_HEAD_Y (PUPU_PET_Y + 2)     // 砸头:落到趴着的噗噗头顶,停到反应结束

static const char *const k_feed[] = {PUPU_TXT_RICE, PUPU_TXT_COOKIE, PUPU_TXT_CANDY};
static const uint8_t k_price[] = {PUPU_RICE_PRICE, PUPU_COOKIE_PRICE, 0};
static const char *const k_play[] = {PUPU_TXT_PAT, PUPU_TXT_WORK, PUPU_TXT_GATE};
enum { PLAY_PAT = 0, PLAY_WORK, PLAY_GAME };
_Static_assert(PUPU_FOOD_RICE == 0 && PUPU_FOOD_COOKIE == 1 && PUPU_FOOD_CANDY == 2,
               "k_feed and k_price are indexed by pupu_food_t");
_Static_assert(PUPU_ICON_HAND_W == PUPU_ICON_SHEET_W, "both overlays share OVERLAY_X");

// 叠层(小手或表格)动画的阶段
typedef enum {
    OV_NONE = 0,
    OV_HAND_FALL,
    OV_HAND_HOLD,
    OV_HAND_LIFT,
    OV_HAND_FLY,
    OV_SHEET_FALL,
    OV_SHEET_STAY,
} overlay_t;

static pupu_care_t s_kind;
static int s_selected[2];  // 两个页面各自记住上次选的那一项
static pupu_picker_t s_picker;
static pupu_bubble_t s_bubble;
static lv_obj_t *s_candy;  // 喂食页右上角的"糖 ×N"
static lv_obj_t *s_coin;   // 它左边的"零花钱 N"
static int s_shown_candy;
static int s_shown_coin;
static bool s_shown_afford;
static pupu_pose_t s_pose;  // 正在做的反应,IDLE 表示没有
static bool s_pose_wait;    // 反应等叠层落地才开始(落地前先显示空闲姿势)
static uint32_t s_pose_until_ms;
static const char *s_msg;
static uint32_t s_msg_until_ms;
static char s_buf[2][80];  // 轮流用,保证气泡每次拿到新指针(见 pupu_bubble_show)
static int s_buf_i;

static lv_image_dsc_t s_hand_dsc;
static lv_image_dsc_t s_sheet_dsc;
static lv_obj_t *s_hand;
static lv_obj_t *s_sheet;
static overlay_t s_ov;
static lv_obj_t *s_ov_obj;  // 正在动的那个图标
static int s_ov_step;
static int s_ov_x;
static int s_ov_y;
static int s_ov_land_y;
static int s_ov_dy;        // 表格每步落多少像素
static int s_ov_land_sfx;  // 落地时放的音效(pupu_sfx_t),-1 不放
static bool s_ov_fly;      // 小手落地后被弹开(第 5 次)
static bool s_ov_hide_on_land;  // 表格被接住:落地就藏起

static void icon_dsc(lv_image_dsc_t *d, const uint8_t *data, int w, uint32_t bytes) {
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565A8;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)w;
    d->header.stride = (uint32_t)w * 2;  // RGB565 那一层的行宽
    d->data_size = bytes;
    d->data = data;
}

static void overlay_hide(void) {
    s_ov = OV_NONE;
    s_ov_obj = NULL;
    lv_obj_add_flag(s_hand, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void overlay_place(void) {
    lv_obj_set_pos(s_ov_obj, s_ov_x, s_ov_y);
}

// 小手从头顶上方落下。fly=true 时落地后被弹开(第 5 次),否则停一下再抬起。
static void overlay_hand(bool fly, int land_sfx) {
    s_ov = OV_HAND_FALL;
    s_ov_obj = s_hand;
    s_ov_step = 0;
    s_ov_x = OVERLAY_X;
    s_ov_land_y = HAND_LAND_Y;
    s_ov_y = s_ov_land_y - FALL_STEPS * FALL_PX;
    s_ov_land_sfx = land_sfx;
    s_ov_fly = fly;
    overlay_place();
    lv_obj_remove_flag(s_hand, LV_OBJ_FLAG_HIDDEN);
}

// 表格从屏幕上方掉下来。to_head=true 砸在头上并留着,否则落到身体前被接住(落地就藏起)。
static void overlay_sheet(bool to_head, int land_sfx) {
    s_ov = OV_SHEET_FALL;
    s_ov_obj = s_sheet;
    s_ov_step = 0;
    s_ov_x = OVERLAY_X;
    s_ov_land_y = to_head ? SHEET_HEAD_Y : SHEET_CATCH_Y;
    s_ov_y = -PUPU_ICON_SHEET_W;  // 整个在屏幕上面
    s_ov_dy = (s_ov_land_y - s_ov_y + SHEET_STEPS - 1) / SHEET_STEPS;
    s_ov_land_sfx = land_sfx;
    s_ov_hide_on_land = !to_head;
    overlay_place();
    lv_obj_remove_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
}

// 叠层落地:放音效,噗噗开始做反应。
static void overlay_landed(const pupu_model_t *m, uint32_t now_ms) {
    if (s_ov_land_sfx >= 0) pupu_sfx_play((pupu_sfx_t)s_ov_land_sfx);
    if (s_pose_wait) {
        s_pose_wait = false;
        pupu_pet_view_play(pupu_ui_pet_anim(m, s_pose), true, now_ms);
    }
}

// 主循环每圈挪一步。
static void overlay_tick(const pupu_model_t *m, uint32_t now_ms) {
    switch (s_ov) {
    case OV_HAND_FALL:
        s_ov_y += FALL_PX;
        if (++s_ov_step >= FALL_STEPS) {
            s_ov_y = s_ov_land_y;
            s_ov_step = 0;
            s_ov = s_ov_fly ? OV_HAND_FLY : OV_HAND_HOLD;
            overlay_landed(m, now_ms);
        }
        overlay_place();
        break;
    case OV_HAND_HOLD:
        if (++s_ov_step >= HOLD_STEPS) {
            s_ov_step = 0;
            s_ov = OV_HAND_LIFT;
        }
        break;
    case OV_HAND_LIFT:
        s_ov_y -= FALL_PX;
        overlay_place();
        if (++s_ov_step >= FALL_STEPS) overlay_hide();
        break;
    case OV_HAND_FLY:
        s_ov_x += FLY_DX;
        s_ov_y -= FLY_DY;
        overlay_place();
        if (++s_ov_step >= FLY_STEPS) overlay_hide();
        break;
    case OV_SHEET_FALL:
        s_ov_y += s_ov_dy;
        if (++s_ov_step >= SHEET_STEPS || s_ov_y >= s_ov_land_y) {
            s_ov_y = s_ov_land_y;
            overlay_landed(m, now_ms);
            if (s_ov_hide_on_land) {
                overlay_hide();
                break;
            }
            s_ov = OV_SHEET_STAY;
        }
        overlay_place();
        break;
    default:  // OV_NONE、OV_SHEET_STAY:不动,反应结束时一起藏起
        break;
    }
}

lv_obj_t *pupu_ui_care_create(pupu_care_t kind, const pupu_model_t *m, uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();
    s_kind = kind;
    pupu_ui_page_frame(scr, kind == PUPU_CARE_FEED ? PUPU_TXT_MENU_FEED : PUPU_TXT_MENU_PLAY);
    s_candy = NULL;
    s_coin = NULL;
    if (kind == PUPU_CARE_FEED) {
        s_candy = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
        s_coin = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
        s_shown_candy = -1;
        s_shown_coin = -1;
        s_shown_afford = true;
    }
    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    // 叠层图标建在宠物之后(压在宠物上面)、气泡之前(气泡压在它上面)
    icon_dsc(&s_hand_dsc, pupu_icon_hand, PUPU_ICON_HAND_W, PUPU_ICON_HAND_BYTES);
    icon_dsc(&s_sheet_dsc, pupu_icon_sheet, PUPU_ICON_SHEET_W, PUPU_ICON_SHEET_BYTES);
    s_hand = lv_image_create(scr);
    lv_image_set_src(s_hand, &s_hand_dsc);
    s_sheet = lv_image_create(scr);
    lv_image_set_src(s_sheet, &s_sheet_dsc);
    overlay_hide();
    pupu_bubble_create(&s_bubble, scr);
    if (kind == PUPU_CARE_FEED) {
        pupu_picker_create(&s_picker, scr, k_feed, 3, s_selected[kind], false);
    } else {
        pupu_picker_create(&s_picker, scr, k_play, 3, s_selected[kind], false);
    }
    s_pose = PUPU_POSE_IDLE;
    s_pose_wait = false;
    s_msg = NULL;
    pupu_ui_care_update(m, now_ms);
    return scr;
}

static void say(const char *text, uint32_t now_ms) {
    s_msg = text;
    s_msg_until_ms = now_ms + MSG_MS;
}

// 马上开始做反应。
static void react(const pupu_model_t *m, pupu_pose_t pose, uint32_t ms, uint32_t now_ms) {
    s_pose = pose;
    s_pose_wait = false;
    s_pose_until_ms = now_ms + ms;
    pupu_pet_view_play(pupu_ui_pet_anim(m, pose), true, now_ms);
}

// 等叠层落地(fall_steps 步以后)再开始做反应,反应从落地起放 ms 毫秒。
static void react_on_land(pupu_pose_t pose, uint32_t ms, int fall_steps, uint32_t now_ms) {
    s_pose = pose;
    s_pose_wait = true;
    s_pose_until_ms = now_ms + (uint32_t)fall_steps * STEP_MS + ms;
}

static char *next_buf(void) {
    s_buf_i ^= 1;
    s_buf[s_buf_i][0] = '\0';
    return s_buf[s_buf_i];
}

static void append(char *buf, size_t cap, const char *name, int value) {
    const size_t n = strlen(buf);
    if (n < cap) snprintf(buf + n, cap - n, "%s%s %+d", n > 0 ? "  " : "", name, value);
}

// 这次实际变了多少。主要的那个数(喂饭团、饼干是饱腹,星星糖和摸摸是开心)总是写,
// 其他的只在变了时写。零花钱的变化不写进气泡(右上角的数字会变)。
static const char *outcome_text(const pupu_outcome_t *o, bool full_first) {
    char *buf = next_buf();
    const size_t cap = sizeof(s_buf[0]);
    if (o->result == PUPU_TIRED) snprintf(buf, cap, "%s", PUPU_TXT_TIRED);
    if (full_first) append(buf, cap, PUPU_TXT_FULL, o->full);
    if (!full_first || o->happy != 0) append(buf, cap, PUPU_TXT_HAPPY, o->happy);
    if (o->bond != 0) append(buf, cap, PUPU_TXT_BOND, o->bond);
    return buf;
}

// 第 5 次摸:"别摸啦！  开心 -5"
static const char *angry_text(const pupu_outcome_t *o) {
    char *buf = next_buf();
    snprintf(buf, sizeof(s_buf[0]), "%s  %s %+d", PUPU_TXT_ANGRY, PUPU_TXT_HAPPY, o->happy);
    return buf;
}

// 打工 1、2 次:"零花钱 +10  开心 -5" 换行 "今天亲密额度 -3"(一行放不下 240 px)
static const char *work_text(const pupu_outcome_t *o) {
    char *buf = next_buf();
    snprintf(buf, sizeof(s_buf[0]), "%s %+d  %s %+d\n%s %d", PUPU_TXT_COIN, o->coin, PUPU_TXT_HAPPY,
             o->happy, PUPU_TXT_BOND_BUDGET, o->bond_budget);
    return buf;
}

// 打工 4、5 次:"做错表了…" 换行 "零花钱 +3  开心 -10"
static const char *overworked_text(const pupu_outcome_t *o) {
    char *buf = next_buf();
    snprintf(buf, sizeof(s_buf[0]), "%s\n%s %+d  %s %+d", PUPU_TXT_EMO_WORK, PUPU_TXT_COIN, o->coin,
             PUPU_TXT_HAPPY, o->happy);
    return buf;
}

static void log_outcome(const char *what, const pupu_outcome_t *o, const pupu_model_t *m) {
    ESP_LOGI(TAG, "%s: 结果 %d 变化 饱腹%+d 开心%+d 亲密+%u 星星糖%+d 零花钱%+d 额度%d%s%s → "
                  "饱腹=%d 开心=%d 亲密=%d 星星糖=%d 零花钱=%d 今天打工=%d emo剩=%u分",
             what, (int)o->result, o->full, o->happy, (unsigned)o->bond, o->candy, o->coin,
             o->bond_budget, o->emo_cleared ? " 解除emo" : "", o->grew ? " 长大了" : "", m->full,
             m->happy, m->bond, m->candy, m->coin, m->work_today, (unsigned)m->emo_min);
}

static pupu_ui_req_t feed(pupu_model_t *m, pupu_food_t food, uint32_t acct_now, uint32_t now_ms) {
    const pupu_outcome_t o = pupu_model_feed(m, food, acct_now);
    log_outcome(k_feed[food], &o, m);
    switch (o.result) {
    case PUPU_OK:
    case PUPU_TIRED:
        react(m, PUPU_POSE_EAT, EAT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_EAT);
        if (o.grew) pupu_sfx_play(PUPU_SFX_GROW);
        say(o.grew ? PUPU_TXT_GREW : outcome_text(&o, food != PUPU_FOOD_CANDY), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_REFUSED:  // 太饱,摇头不吃
        react(m, PUPU_POSE_SHAKE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_NO);
        say(PUPU_TXT_REFUSED, now_ms);
        return PUPU_UI_NONE;
    case PUPU_NO_CANDY:
        pupu_sfx_play(PUPU_SFX_NO);
        say(PUPU_TXT_NO_CANDY, now_ms);
        return PUPU_UI_NONE;
    case PUPU_NO_COIN:  // 钱不够,摇头
        react(m, PUPU_POSE_SHAKE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_NO);
        say(PUPU_TXT_NO_COIN, now_ms);
        return PUPU_UI_NONE;
    case PUPU_SULKING:  // emo 中没胃口吃饼干,摇头叹气
        react(m, PUPU_POSE_SHAKE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_SIGH);
        say(PUPU_TXT_NO_APPETITE, now_ms);
        return PUPU_UI_NONE;
    default:
        return PUPU_UI_NONE;
    }
}

static pupu_ui_req_t pat(pupu_model_t *m, uint32_t now_ms) {
    const pupu_outcome_t o = pupu_model_pat(m);
    log_outcome(PUPU_TXT_PAT, &o, m);
    switch (o.result) {
    case PUPU_OK:  // 第 1–3 次:小手拍头,"啪",开心跳一下
        react_on_land(PUPU_POSE_HAPPY, REACT_MS, FALL_STEPS, now_ms);
        overlay_hand(false, PUPU_SFX_PAT);
        if (o.grew) pupu_sfx_play(PUPU_SFX_GROW);
        say(o.grew ? PUPU_TXT_GREW : outcome_text(&o, false), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_ANNOYED:  // 第 4 次:小手落下,"哼",皱眉侧头
        react_on_land(PUPU_POSE_ANNOYED, REACT_MS, FALL_STEPS, now_ms);
        overlay_hand(false, PUPU_SFX_ANNOYED);
        say(PUPU_TXT_ANNOYED, now_ms);
        return PUPU_UI_CHANGED;  // 计了数,存档
    case PUPU_ANGRY:  // 第 5 次:小手被弹开,三连降,生气跳开
        react_on_land(PUPU_POSE_ANGRY, REACT_MS, FALL_STEPS, now_ms);
        overlay_hand(true, PUPU_SFX_ANGRY);
        say(angry_text(&o), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_DODGED:  // 第 6 次起:只转身,不出小手、不出气泡、不出声
        react(m, PUPU_POSE_DODGE, REACT_MS, now_ms);
        return PUPU_UI_NONE;
    case PUPU_SULKING:  // emo 中:躲开,叹气,"不想理你"
        react(m, PUPU_POSE_DODGE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_SIGH);
        say(PUPU_TXT_SULKING, now_ms);
        return PUPU_UI_NONE;
    default:
        return PUPU_UI_NONE;
    }
}

static pupu_ui_req_t work(pupu_model_t *m, uint32_t now_ms) {
    const pupu_outcome_t o = pupu_model_work(m);
    log_outcome(PUPU_TXT_WORK, &o, m);
    switch (o.result) {
    case PUPU_OK:  // 第 1、2 次:接住表格,"叮叮"
        react_on_land(PUPU_POSE_WORK, WORK_MS, SHEET_STEPS, now_ms);
        overlay_sheet(false, PUPU_SFX_COIN);
        say(work_text(&o), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_WENT_EMO:  // 第 3 次:表格砸中头,"砰",进入 emo
        react_on_land(PUPU_POSE_DIZZY, WORK_MS, SHEET_STEPS, now_ms);
        overlay_sheet(true, PUPU_SFX_THUD);
        say(pupu_ui_emo_text(m), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_OVERWORKED:  // 第 4、5 次:做错表了
        react_on_land(PUPU_POSE_DIZZY, WORK_MS, SHEET_STEPS, now_ms);
        overlay_sheet(true, PUPU_SFX_THUD);
        say(overworked_text(&o), now_ms);
        return PUPU_UI_CHANGED;
    case PUPU_EXHAUSTED:  // 第 6 次起:摇头,"做不动了"
        react(m, PUPU_POSE_SHAKE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_NO);
        say(PUPU_TXT_EXHAUSTED, now_ms);
        return PUPU_UI_NONE;
    default:
        return PUPU_UI_NONE;
    }
}

pupu_ui_req_t pupu_ui_care_action(pupu_action_t act, pupu_model_t *m, uint32_t acct_now,
                                  uint32_t now_ms) {
    if (pupu_picker_action(&s_picker, act)) {
        s_selected[s_kind] = s_picker.selected;
        return PUPU_UI_NONE;
    }
    if (act == PUPU_ACT_BACK) return PUPU_UI_GO_HOME;
    if (act != PUPU_ACT_CONFIRM) return PUPU_UI_NONE;
    if (s_pose != PUPU_POSE_IDLE) return PUPU_UI_NONE;  // 还在吃或者躲,等它做完
    if (s_kind == PUPU_CARE_FEED) return feed(m, (pupu_food_t)s_picker.selected, acct_now, now_ms);
    switch (s_picker.selected) {
    case PLAY_PAT: return pat(m, now_ms);
    case PLAY_WORK: return work(m, now_ms);
    default: return PUPU_UI_GO_GATE;
    }
}

bool pupu_ui_care_busy(void) {
    return s_pose != PUPU_POSE_IDLE;
}

// 喂食页右上角:"零花钱 N  糖 ×N",两个短标签右对齐;买不起时胶囊里的字变淡。
static void refresh_feed_hud(const pupu_model_t *m) {
    if (m->candy != s_shown_candy || m->coin != s_shown_coin) {
        s_shown_candy = m->candy;
        s_shown_coin = m->coin;
        lv_label_set_text_fmt(s_candy, PUPU_TXT_CANDY_SHORT, m->candy);
        lv_obj_align(s_candy, LV_ALIGN_TOP_RIGHT, -22, 10);
        lv_label_set_text_fmt(s_coin, PUPU_TXT_COIN_COUNT, m->coin);
        lv_obj_update_layout(s_candy);
        lv_obj_align_to(s_coin, s_candy, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    }
    const bool afford = m->coin >= k_price[s_picker.selected];
    if (afford != s_shown_afford) {
        s_shown_afford = afford;
        lv_obj_set_style_text_color(s_picker.mid,
                                    lv_color_hex(afford ? PUPU_COLOR_PAPER : PUPU_COLOR_MUTED), 0);
    }
}

void pupu_ui_care_update(const pupu_model_t *m, uint32_t now_ms) {
    if (s_pose != PUPU_POSE_IDLE && (int32_t)(now_ms - s_pose_until_ms) >= 0) {
        s_pose = PUPU_POSE_IDLE;
        s_pose_wait = false;
        overlay_hide();
    }
    // 没在反应(或者反应还在等叠层落地)时用空闲姿势:很饿趴着 > emo > 不耐烦 > 普通
    const pupu_pose_t pose = s_pose == PUPU_POSE_IDLE || s_pose_wait ? pupu_ui_idle_pose(m) : s_pose;
    pupu_pet_view_play(pupu_ui_pet_anim(m, pose), true, now_ms);
    pupu_pet_view_set_tint(pupu_tint_step(m->full, m->happy));
    pupu_pet_view_tick(now_ms);
    overlay_tick(m, now_ms);

    if (s_msg && (int32_t)(now_ms - s_msg_until_ms) >= 0) s_msg = NULL;
    if (s_msg) {
        pupu_bubble_show(&s_bubble, s_msg);
    } else if (s_pose == PUPU_POSE_IDLE && m->full < PUPU_HUNGRY_BELOW) {
        pupu_bubble_show(&s_bubble, PUPU_TXT_HUNGRY);  // "饿了"排在"累坏了"前面
    } else if (s_pose == PUPU_POSE_IDLE && m->emo_min > 0) {
        pupu_bubble_show(&s_bubble, pupu_ui_emo_text(m));
    } else {
        pupu_bubble_show(&s_bubble, NULL);
    }

    if (s_candy) refresh_feed_hud(m);
}

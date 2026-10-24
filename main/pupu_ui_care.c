// main/pupu_ui_care.c —— 喂食页和玩耍页:宠物在中间,上面是结果气泡,下面是选择条。
//
// 喂食:饭团 / 小饼干 / 星星糖(右上角显示库存)。玩耍:摸摸头 / 接星星(小游戏在 M5)。
// ↑/↓ 选,OK 确认,长按 OK 回首页(产品说明第 7 节)。
// 确认以后噗噗先做一个反应(吃 2 秒、开心、躲开、摇头),做完之前再按 OK 不响应;
// 气泡里写这次实际变了多少(已经过上限截断),比如"饱腹 +35  亲密 +3"。
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

#define EAT_MS 2000    // 咀嚼动画放多久
#define REACT_MS 1200  // 开心、躲开、摇头放多久
#define MSG_MS 2500    // 结果气泡显示多久

static const char *const k_feed[] = {PUPU_TXT_RICE, PUPU_TXT_COOKIE, PUPU_TXT_CANDY};
static const char *const k_play[] = {PUPU_TXT_PAT, PUPU_TXT_CATCH};
_Static_assert(PUPU_FOOD_RICE == 0 && PUPU_FOOD_COOKIE == 1 && PUPU_FOOD_CANDY == 2,
               "k_feed is indexed by pupu_food_t");

static pupu_care_t s_kind;
static int s_selected[2];  // 两个页面各自记住上次选的那一项
static pupu_picker_t s_picker;
static pupu_bubble_t s_bubble;
static lv_obj_t *s_candy;  // 喂食页右上角的库存
static int s_shown_candy;
static pupu_pose_t s_pose;
static uint32_t s_pose_until_ms;
static const char *s_msg;
static uint32_t s_msg_until_ms;
static char s_buf[2][64];  // 轮流用,保证气泡每次拿到新指针(见 pupu_bubble_show)
static int s_buf_i;

lv_obj_t *pupu_ui_care_create(pupu_care_t kind, const pupu_model_t *m, uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();
    s_kind = kind;
    pupu_ui_page_frame(scr, kind == PUPU_CARE_FEED ? PUPU_TXT_MENU_FEED : PUPU_TXT_MENU_PLAY);
    s_candy = NULL;
    if (kind == PUPU_CARE_FEED) {
        s_candy = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
        s_shown_candy = -1;
    }
    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    pupu_bubble_create(&s_bubble, scr);
    if (kind == PUPU_CARE_FEED) {
        pupu_picker_create(&s_picker, scr, k_feed, 3, s_selected[kind], false);
    } else {
        pupu_picker_create(&s_picker, scr, k_play, 2, s_selected[kind], false);
    }
    s_pose = PUPU_POSE_IDLE;
    s_msg = NULL;
    pupu_ui_care_update(m, now_ms);
    return scr;
}

static void say(const char *text, uint32_t now_ms) {
    s_msg = text;
    s_msg_until_ms = now_ms + MSG_MS;
}

static void react(const pupu_model_t *m, pupu_pose_t pose, uint32_t ms, uint32_t now_ms) {
    s_pose = pose;
    s_pose_until_ms = now_ms + ms;
    pupu_pet_view_play(pupu_ui_pet_anim(m, pose), true, now_ms);
}

static void append(char *buf, size_t cap, const char *name, int value) {
    const size_t n = strlen(buf);
    if (n < cap) snprintf(buf + n, cap - n, "%s%s %+d", n > 0 ? "  " : "", name, value);
}

// 这次实际变了多少。主要的那个数(喂饭团、饼干是饱腹,星星糖和摸摸是开心)总是写,
// 其他的只在变了时写。
static const char *outcome_text(const pupu_outcome_t *o, bool full_first) {
    s_buf_i ^= 1;
    char *buf = s_buf[s_buf_i];
    const size_t cap = sizeof(s_buf[0]);
    buf[0] = '\0';
    if (o->result == PUPU_TIRED) snprintf(buf, cap, "%s", PUPU_TXT_TIRED);
    if (full_first) append(buf, cap, PUPU_TXT_FULL, o->full);
    if (!full_first || o->happy != 0) append(buf, cap, PUPU_TXT_HAPPY, o->happy);
    if (o->bond != 0) append(buf, cap, PUPU_TXT_BOND, o->bond);
    return buf;
}

static void log_outcome(const char *what, const pupu_outcome_t *o, const pupu_model_t *m) {
    ESP_LOGI(TAG, "%s: 结果 %d 变化 饱腹%+d 开心%+d 亲密+%u 星星糖%+d%s → 饱腹=%d 开心=%d 亲密=%d 星星糖=%d",
             what, (int)o->result, o->full, o->happy, (unsigned)o->bond, o->candy,
             o->grew ? " 长大了" : "", m->full, m->happy, m->bond, m->candy);
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
    default:
        return PUPU_UI_NONE;
    }
}

static pupu_ui_req_t pat(pupu_model_t *m, uint32_t now_ms) {
    const pupu_outcome_t o = pupu_model_pat(m);
    log_outcome(PUPU_TXT_PAT, &o, m);
    if (o.result == PUPU_DODGED) {
        react(m, PUPU_POSE_DODGE, REACT_MS, now_ms);
        pupu_sfx_play(PUPU_SFX_NO);
        say(PUPU_TXT_DODGED, now_ms);
        return PUPU_UI_NONE;
    }
    if (o.result != PUPU_OK) return PUPU_UI_NONE;
    react(m, PUPU_POSE_HAPPY, REACT_MS, now_ms);
    pupu_sfx_play(o.grew ? PUPU_SFX_GROW : PUPU_SFX_HAPPY);
    say(o.grew ? PUPU_TXT_GREW : outcome_text(&o, false), now_ms);
    return PUPU_UI_CHANGED;
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
    if (s_picker.selected == 0) return pat(m, now_ms);
    ESP_LOGI(TAG, "接星星在 M5 才做");
    say(PUPU_TXT_NOT_YET, now_ms);
    return PUPU_UI_NONE;
}

void pupu_ui_care_update(const pupu_model_t *m, uint32_t now_ms) {
    if (s_pose != PUPU_POSE_IDLE && (int32_t)(now_ms - s_pose_until_ms) >= 0) {
        s_pose = PUPU_POSE_IDLE;
    }
    pupu_pet_view_play(pupu_ui_pet_anim(m, s_pose), true, now_ms);
    pupu_pet_view_set_tint(pupu_tint_step(m->full, m->happy));
    pupu_pet_view_tick(now_ms);

    if (s_msg && (int32_t)(now_ms - s_msg_until_ms) >= 0) s_msg = NULL;
    if (s_msg) {
        pupu_bubble_show(&s_bubble, s_msg);
    } else if (s_pose == PUPU_POSE_IDLE && m->full < PUPU_HUNGRY_BELOW) {
        pupu_bubble_show(&s_bubble, PUPU_TXT_HUNGRY);
    } else {
        pupu_bubble_show(&s_bubble, NULL);
    }

    if (s_candy && m->candy != s_shown_candy) {
        s_shown_candy = m->candy;
        lv_label_set_text_fmt(s_candy, PUPU_TXT_CANDY_COUNT, m->candy);
        lv_obj_align(s_candy, LV_ALIGN_TOP_RIGHT, -22, 10);
    }
}

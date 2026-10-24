// main/pupu_ui_home.c —— 首页:顶栏电量、需求气泡、宠物画布、底部 4 个功能。
//
// 首页不显示数字,只用表情、毛色和最多一个气泡表达状态(产品说明第 2、8 节)。
// ↑/↓ 切换底部功能,到头就停;OK 单击进入喂食、玩耍、状态、同步;长按 OK 熄屏。
// 从别的页面回来时,底部还停在上次选的那一项。
#include "pupu_ui.h"

#include "pupu_config.h"
#include "pupu_pet_view.h"
#include "pupu_sprite_math.h"
#include "pupu_text.h"

#define MENU_COUNT 4

static const char *const k_menu[MENU_COUNT] = {
    PUPU_TXT_MENU_FEED, PUPU_TXT_MENU_PLAY, PUPU_TXT_MENU_STATUS, PUPU_TXT_MENU_SYNC,
};

static lv_obj_t *s_battery;
static pupu_bubble_t s_bubble;
static pupu_picker_t s_picker;
static int s_selected;  // 换页后保留
static int s_shown_battery;
static const char *s_note;  // 临时气泡(开机提示、"你回来啦")
static uint32_t s_note_until_ms;

static const char *battery_symbol(int soc) {
    if (soc >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (soc >= 60) return LV_SYMBOL_BATTERY_3;
    if (soc >= 35) return LV_SYMBOL_BATTERY_2;
    if (soc >= 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void refresh_battery(int soc) {
    if (soc == s_shown_battery) return;
    s_shown_battery = soc;
    if (soc < 0) {
        // 读不到电量时不显示数字(ai-guide:不能画一个假的数)
        lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text_fmt(s_battery, "%s %d%%", battery_symbol(soc), soc);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -18, 10);
    lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
}

static void set_note(const char *text, uint32_t until_ms) {
    s_note = text;
    s_note_until_ms = until_ms;
}

lv_obj_t *pupu_ui_home_create(const pupu_model_t *m, int battery, const char *notice,
                              uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();

    s_battery = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
    s_shown_battery = -1;

    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    pupu_bubble_create(&s_bubble, scr);
    pupu_picker_create(&s_picker, scr, k_menu, MENU_COUNT, s_selected, true);
    set_note(notice, now_ms + PUPU_NOTICE_MS);

    pupu_ui_home_update(m, battery, now_ms);
    return scr;
}

pupu_ui_req_t pupu_ui_home_action(pupu_action_t act, uint32_t now_ms) {
    (void)now_ms;
    if (pupu_picker_action(&s_picker, act)) {
        s_selected = s_picker.selected;
        return PUPU_UI_NONE;
    }
    if (act == PUPU_ACT_CONFIRM) {
        switch (s_picker.selected) {
        case 0: return PUPU_UI_GO_FEED;
        case 1: return PUPU_UI_GO_PLAY;
        case 2: return PUPU_UI_GO_STATUS;
        default: return PUPU_UI_GO_SYNC;
        }
    }
    if (act == PUPU_ACT_BACK) return PUPU_UI_SCREEN_OFF;
    return PUPU_UI_NONE;
}

void pupu_ui_home_update(const pupu_model_t *m, int battery, uint32_t now_ms) {
    pupu_pet_view_play(pupu_ui_pet_anim(m, PUPU_POSE_IDLE), true, now_ms);
    pupu_pet_view_set_tint(pupu_tint_step(m->full, m->happy));
    pupu_pet_view_tick(now_ms);

    if (s_note && (int32_t)(now_ms - s_note_until_ms) >= 0) s_note = NULL;
    if (s_note) {
        pupu_bubble_show(&s_bubble, s_note);
    } else if (m->full < PUPU_HUNGRY_BELOW) {
        pupu_bubble_show(&s_bubble, PUPU_TXT_HUNGRY);
    } else {
        pupu_bubble_show(&s_bubble, NULL);
    }
    refresh_battery(battery);
}

// main/pupu_ui_home.c —— 首页:顶栏电量、需求气泡、宠物画布、底部 4 个功能。
//
// 首页不显示数字,只用表情、毛色和最多一个气泡表达状态(产品说明第 2、8 节)。
// ↑/↓ 切换底部功能,到头就停;OK 单击进入(喂食、玩耍、状态从 M3 起,同步在 M6)。
#include "pupu_ui.h"

#include "bsp_battery.h"
#include "pupu_config.h"
#include "pupu_fonts.h"
#include "pupu_pet_view.h"
#include "pupu_sprite_math.h"
#include "pupu_text.h"
#include "esp_log.h"

static const char *TAG = "pupu_home";

#define MENU_COUNT 4
#define MENU_Y 250
#define MENU_H 30
#define MID_W 72
#define DOT_Y 292
#define NOTE_MS 2000            // "还没做好"气泡显示多久
#define BATTERY_EVERY_MS 30000  // 多久读一次电量

static const char *const k_menu[MENU_COUNT] = {
    PUPU_TXT_MENU_FEED, PUPU_TXT_MENU_PLAY, PUPU_TXT_MENU_STATUS, PUPU_TXT_MENU_SYNC,
};

static lv_obj_t *s_battery;
static lv_obj_t *s_bubble;
static lv_obj_t *s_bubble_text;
static lv_obj_t *s_left;
static lv_obj_t *s_mid_text;
static lv_obj_t *s_right;
static lv_obj_t *s_dots[MENU_COUNT];

static int s_selected;
static int s_anim = -1;
static const char *s_bubble_shown;
static bool s_note;
static uint32_t s_note_until_ms;
static bool s_battery_read;
static uint32_t s_battery_ms;

static void refresh_menu(void) {
    lv_label_set_text(s_left, s_selected > 0 ? k_menu[s_selected - 1] : "");
    lv_label_set_text(s_mid_text, k_menu[s_selected]);
    lv_label_set_text(s_right, s_selected < MENU_COUNT - 1 ? k_menu[s_selected + 1] : "");
    lv_obj_align(s_left, LV_ALIGN_TOP_MID, -80, MENU_Y + 5);
    lv_obj_center(s_mid_text);
    lv_obj_align(s_right, LV_ALIGN_TOP_MID, 80, MENU_Y + 5);
    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i == s_selected ? PUPU_COLOR_INK : PUPU_COLOR_SOFT), 0);
    }
}

static const char *battery_symbol(int soc) {
    if (soc >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (soc >= 60) return LV_SYMBOL_BATTERY_3;
    if (soc >= 35) return LV_SYMBOL_BATTERY_2;
    if (soc >= 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void refresh_battery(void) {
    const int soc = bsp_battery_soc();
    if (soc < 0) {
        // 读不到电量时不显示数字(ai-guide:不能画一个假的数)
        lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text_fmt(s_battery, "%s %d%%", battery_symbol(soc), soc);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -18, 10);
    lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
}

static void show_bubble(const char *text) {
    if (text == s_bubble_shown) return;
    s_bubble_shown = text;
    if (text == NULL) {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(s_bubble_text, text);
    lv_obj_align(s_bubble, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_remove_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *pupu_ui_home_create(const pupu_model_t *m, uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();

    s_battery = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);

    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    s_anim = -1;

    s_bubble = lv_obj_create(scr);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(PUPU_COLOR_PAPER), 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bubble, lv_color_hex(PUPU_COLOR_INK), 0);
    lv_obj_set_style_border_width(s_bubble, 2, 0);
    lv_obj_set_style_radius(s_bubble, 12, 0);
    lv_obj_set_style_pad_hor(s_bubble, 12, 0);
    lv_obj_set_style_pad_ver(s_bubble, 4, 0);
    lv_obj_remove_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_bubble_text = pupu_ui_label(s_bubble, "", PUPU_COLOR_INK);
    lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    s_bubble_shown = NULL;
    s_note = false;

    s_left = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    lv_obj_t *mid = pupu_ui_rect(scr, (240 - MID_W) / 2, MENU_Y, MID_W, MENU_H,
                                 PUPU_COLOR_INK, MENU_H / 2);
    s_mid_text = pupu_ui_label(mid, "", PUPU_COLOR_PAPER);
    s_right = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    for (int i = 0; i < MENU_COUNT; i++) {
        s_dots[i] = pupu_ui_rect(scr, 120 - 2 * 12 + i * 12 + 3, DOT_Y, 6, 6, PUPU_COLOR_SOFT, 3);
    }
    s_selected = 0;
    refresh_menu();

    s_battery_read = false;
    pupu_ui_home_update(m, now_ms);
    return scr;
}

void pupu_ui_home_action(pupu_action_t act, uint32_t now_ms) {
    switch (act) {
    case PUPU_ACT_PREV:
        if (s_selected > 0) {
            s_selected--;
            refresh_menu();
        }
        break;
    case PUPU_ACT_NEXT:
        if (s_selected < MENU_COUNT - 1) {
            s_selected++;
            refresh_menu();
        }
        break;
    case PUPU_ACT_CONFIRM:
        // 喂食、玩耍、状态从 M3 起才有页面,同步在 M6;现在先说一句"还没做好"。
        ESP_LOGI(TAG, "选了第 %d 项,这个页面还没做", s_selected);
        s_note = true;
        s_note_until_ms = now_ms + NOTE_MS;
        break;
    case PUPU_ACT_BACK:
        ESP_LOGI(TAG, "长按 OK:熄屏在 M4 才做");
        break;
    default:
        break;
    }
}

void pupu_ui_home_update(const pupu_model_t *m, uint32_t now_ms) {
    pupu_anim_t anim = PUPU_ANIM_CHILD_IDLE;
    if (m->stage == PUPU_STAGE_ADULT) {
        anim = PUPU_ANIM_ADULT_IDLE;
    } else if (m->full < PUPU_STARVING_BELOW) {
        anim = PUPU_ANIM_CHILD_HUNGRY;  // 很饿:趴着不动
    }
    if ((int)anim != s_anim) {
        s_anim = (int)anim;
        pupu_pet_view_play(anim, true, now_ms);
    }
    pupu_pet_view_set_tint(pupu_tint_step(m->full, m->happy));
    pupu_pet_view_tick(now_ms);

    if (s_note && (int32_t)(now_ms - s_note_until_ms) >= 0) s_note = false;
    if (s_note) {
        show_bubble(PUPU_TXT_NOT_YET);
    } else if (m->full < PUPU_HUNGRY_BELOW) {
        show_bubble(PUPU_TXT_HUNGRY);
    } else {
        show_bubble(NULL);
    }

    if (!s_battery_read || now_ms - s_battery_ms >= BATTERY_EVERY_MS) {
        s_battery_read = true;
        s_battery_ms = now_ms;
        refresh_battery();
    }
}

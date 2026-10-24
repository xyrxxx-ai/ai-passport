// main/pupu_ui_egg.c —— 蛋页:按住 OK 捂热 3 秒孵化,中途松手取消。
#include "pupu_ui.h"

#include "pupu_config.h"
#include "pupu_fonts.h"
#include "pupu_pet_view.h"
#include "pupu_text.h"
#include "esp_log.h"

static const char *TAG = "pupu_egg";

#define BAR_X 40
#define BAR_Y 268
#define BAR_W 160
#define BAR_H 8
#define HATCH_LINGER_MS 800  // 孵化动画放完后再停一会儿,让人看清

static lv_obj_t *s_label;
static lv_obj_t *s_track;
static lv_obj_t *s_fill;
static int32_t s_fill_w;
static bool s_hatching;
static bool s_anim_done;
static uint32_t s_done_ms;

static void set_text(const char *text) {
    lv_label_set_text(s_label, text);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, 236);
}

lv_obj_t *pupu_ui_egg_create(uint32_t now_ms) {
    lv_obj_t *scr = pupu_ui_screen_create();
    pupu_pet_view_create(scr, PUPU_PET_X, PUPU_PET_Y);
    pupu_pet_view_play(PUPU_ANIM_EGG_IDLE, true, now_ms);

    s_label = pupu_ui_label(scr, PUPU_TXT_EGG_HINT, PUPU_COLOR_INK);
    set_text(PUPU_TXT_EGG_HINT);

    s_track = pupu_ui_rect(scr, BAR_X, BAR_Y, BAR_W, BAR_H, PUPU_COLOR_SOFT, BAR_H / 2);
    s_fill = pupu_ui_rect(s_track, 0, 0, 0, BAR_H, PUPU_COLOR_ACCENT, BAR_H / 2);
    s_fill_w = 0;
    lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);

    s_hatching = false;
    s_anim_done = false;
    return scr;
}

void pupu_ui_egg_action(pupu_action_t act, pupu_model_t *m, uint32_t now_ms) {
    if (s_hatching) return;  // 孵化动画期间不再响应
    switch (act) {
    case PUPU_ACT_HOLD_START:
        pupu_pet_view_play(PUPU_ANIM_EGG_WOBBLE, true, now_ms);
        set_text(PUPU_TXT_EGG_WARMING);
        lv_obj_remove_flag(s_track, LV_OBJ_FLAG_HIDDEN);
        break;
    case PUPU_ACT_HOLD_CANCEL:
        pupu_pet_view_play(PUPU_ANIM_EGG_IDLE, true, now_ms);
        set_text(PUPU_TXT_EGG_HINT);
        lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
        s_fill_w = 0;
        lv_obj_set_width(s_fill, 0);
        ESP_LOGI(TAG, "松手了,取消孵化");
        break;
    case PUPU_ACT_HATCH:
        pupu_model_hatch(m);
        s_hatching = true;
        pupu_pet_view_play(PUPU_ANIM_EGG_HATCH, false, now_ms);
        set_text(PUPU_TXT_EGG_HATCHED);
        lv_obj_add_flag(s_track, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "孵化了");
        break;
    default:
        break;
    }
}

bool pupu_ui_egg_update(const pupu_input_t *in, uint32_t now_ms) {
    if (pupu_pet_view_tick(now_ms) && s_hatching) {
        s_anim_done = true;
        s_done_ms = now_ms;
    }
    if (!s_hatching && in->holding) {
        const int32_t w = (int32_t)(BAR_W * pupu_input_hold_ms(in, now_ms) / PUPU_HOLD_MS);
        if (w != s_fill_w) {
            s_fill_w = w;
            lv_obj_set_width(s_fill, w);
        }
    }
    return s_anim_done && now_ms - s_done_ms >= HATCH_LINGER_MS;
}

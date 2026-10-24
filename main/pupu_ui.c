// main/pupu_ui.c —— 页面共用的小工具:空白屏幕、文字、纯色方块、气泡、底部选择条。
#include "pupu_ui.h"

#include "pupu_config.h"
#include "pupu_fonts.h"
#include "pupu_text.h"

#define PICKER_Y 250
#define PICKER_H 30
#define PILL_W 72
#define PICKER_SIDE 80  // 两边淡色字离中线多远
#define DOT_Y 292
#define DOT_GAP 12
#define BUBBLE_Y 34

pupu_anim_t pupu_ui_pet_anim(const pupu_model_t *m, pupu_pose_t pose) {
    static const pupu_anim_t child[] = {
        PUPU_ANIM_CHILD_IDLE, PUPU_ANIM_CHILD_EAT, PUPU_ANIM_CHILD_HAPPY,
        PUPU_ANIM_CHILD_DODGE, PUPU_ANIM_CHILD_SHAKE, PUPU_ANIM_CHILD_WAVE,
    };
    static const pupu_anim_t adult[] = {
        PUPU_ANIM_ADULT_IDLE, PUPU_ANIM_ADULT_EAT, PUPU_ANIM_ADULT_HAPPY,
        PUPU_ANIM_ADULT_DODGE, PUPU_ANIM_ADULT_SHAKE, PUPU_ANIM_ADULT_WAVE,
    };
    _Static_assert(sizeof(child) / sizeof(child[0]) == PUPU_POSE_WAVE + 1, "one anim per pose");
    _Static_assert(sizeof(adult) / sizeof(adult[0]) == PUPU_POSE_WAVE + 1, "one anim per pose");
    const bool grown = m->stage == PUPU_STAGE_ADULT;
    if (pose == PUPU_POSE_IDLE && m->full < PUPU_STARVING_BELOW) {
        return grown ? PUPU_ANIM_ADULT_HUNGRY : PUPU_ANIM_CHILD_HUNGRY;  // 很饿:趴着不动
    }
    return grown ? adult[pose] : child[pose];
}

lv_obj_t *pupu_ui_screen_create(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(PUPU_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

lv_obj_t *pupu_ui_label(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &pupu_font_body, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *pupu_ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t color, int32_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void pupu_bubble_create(pupu_bubble_t *b, lv_obj_t *scr) {
    b->box = lv_obj_create(scr);
    lv_obj_remove_style_all(b->box);
    lv_obj_set_size(b->box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b->box, lv_color_hex(PUPU_COLOR_PAPER), 0);
    lv_obj_set_style_bg_opa(b->box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b->box, lv_color_hex(PUPU_COLOR_INK), 0);
    lv_obj_set_style_border_width(b->box, 2, 0);
    lv_obj_set_style_radius(b->box, 12, 0);
    lv_obj_set_style_pad_hor(b->box, 12, 0);
    lv_obj_set_style_pad_ver(b->box, 4, 0);
    lv_obj_remove_flag(b->box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    b->text = pupu_ui_label(b->box, "", PUPU_COLOR_INK);
    lv_obj_add_flag(b->box, LV_OBJ_FLAG_HIDDEN);
    b->shown = NULL;
}

void pupu_bubble_show(pupu_bubble_t *b, const char *text) {
    if (text == b->shown) return;
    b->shown = text;
    if (text == NULL) {
        lv_obj_add_flag(b->box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(b->text, text);
    lv_obj_align(b->box, LV_ALIGN_TOP_MID, 0, BUBBLE_Y);
    lv_obj_remove_flag(b->box, LV_OBJ_FLAG_HIDDEN);
}

static void picker_refresh(pupu_picker_t *p) {
    const int i = p->selected;
    lv_label_set_text(p->left, i > 0 ? p->items[i - 1] : "");
    lv_label_set_text(p->mid, p->items[i]);
    lv_label_set_text(p->right, i < p->count - 1 ? p->items[i + 1] : "");
    lv_obj_align(p->left, LV_ALIGN_TOP_MID, -PICKER_SIDE, PICKER_Y + 5);
    lv_obj_center(p->mid);
    lv_obj_align(p->right, LV_ALIGN_TOP_MID, PICKER_SIDE, PICKER_Y + 5);
    if (!p->has_dots) return;
    for (int k = 0; k < p->count; k++) {
        lv_obj_set_style_bg_color(p->dots[k],
            lv_color_hex(k == i ? PUPU_COLOR_INK : PUPU_COLOR_SOFT), 0);
    }
}

void pupu_picker_create(pupu_picker_t *p, lv_obj_t *scr, const char *const *items, int count,
                        int selected, bool dots) {
    p->items = items;
    p->count = count;
    p->selected = selected >= 0 && selected < count ? selected : 0;
    p->has_dots = dots && count <= PUPU_PICKER_MAX;
    p->left = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    // 每一项最多 3 个字(48 px),定宽的胶囊放得下
    p->pill = pupu_ui_rect(scr, (240 - PILL_W) / 2, PICKER_Y, PILL_W, PICKER_H, PUPU_COLOR_INK,
                           PICKER_H / 2);
    p->mid = pupu_ui_label(p->pill, "", PUPU_COLOR_PAPER);
    p->right = pupu_ui_label(scr, "", PUPU_COLOR_MUTED);
    if (p->has_dots) {
        const int32_t x0 = 120 - count * DOT_GAP / 2 + 3;
        for (int k = 0; k < count; k++) {
            p->dots[k] = pupu_ui_rect(scr, x0 + k * DOT_GAP, DOT_Y, 6, 6, PUPU_COLOR_SOFT, 3);
        }
    }
    picker_refresh(p);
}

bool pupu_picker_action(pupu_picker_t *p, pupu_action_t act) {
    if (act == PUPU_ACT_PREV && p->selected > 0) {
        p->selected--;
    } else if (act == PUPU_ACT_NEXT && p->selected < p->count - 1) {
        p->selected++;
    } else {
        return false;
    }
    picker_refresh(p);
    return true;
}

void pupu_ui_page_frame(lv_obj_t *scr, const char *title) {
    lv_obj_t *t = pupu_ui_label(scr, title, PUPU_COLOR_MUTED);
    lv_obj_set_pos(t, 22, 10);
    lv_obj_t *hint = pupu_ui_label(scr, PUPU_TXT_BACK_HINT, PUPU_COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
}

// main/pupu_ui_status.c —— 状态页:饱腹、开心、星星糖;幼年多一行"成长进度 x/100"
// (就是亲密度,产品说明第 12 节第 10 条)。数字只在这一页显示。
// 一页放得下,所以 ↑/↓ 不用翻页;长按 OK 回首页。
#include "pupu_ui.h"

#include "pupu_config.h"
#include "pupu_text.h"

#define ROW_X 24
#define ROW_W 192
#define ROW_Y0 60
#define ROW_STEP 56
#define BAR_DY 24
#define BAR_H 10

typedef struct {
    lv_obj_t *value;
    lv_obj_t *fill;  // NULL 表示这一行没有进度条
    int y;
    int shown;
} row_t;

enum { ROW_FULL, ROW_HAPPY, ROW_CANDY, ROW_GROWTH, ROW_COUNT };
static row_t s_rows[ROW_COUNT];
static bool s_growth;

static void row_create(lv_obj_t *scr, int i, const char *name, bool bar) {
    row_t *r = &s_rows[i];
    r->y = ROW_Y0 + i * ROW_STEP;
    lv_obj_t *label = pupu_ui_label(scr, name, PUPU_COLOR_INK);
    lv_obj_set_pos(label, ROW_X, r->y);
    r->value = pupu_ui_label(scr, "", PUPU_COLOR_INK);
    r->fill = NULL;
    if (bar) {
        lv_obj_t *track = pupu_ui_rect(scr, ROW_X, r->y + BAR_DY, ROW_W, BAR_H, PUPU_COLOR_SOFT,
                                       BAR_H / 2);
        r->fill = pupu_ui_rect(track, 0, 0, 0, BAR_H, PUPU_COLOR_ACCENT, BAR_H / 2);
    }
    r->shown = -1;
}

static void row_set(int i, int value, int max) {
    row_t *r = &s_rows[i];
    if (value == r->shown) return;
    r->shown = value;
    if (i == ROW_GROWTH) {
        lv_label_set_text_fmt(r->value, "%d/%d", value, max);
    } else {
        lv_label_set_text_fmt(r->value, "%d", value);
    }
    lv_obj_align(r->value, LV_ALIGN_TOP_RIGHT, -(240 - ROW_X - ROW_W), r->y);
    if (r->fill) lv_obj_set_width(r->fill, ROW_W * (value < max ? value : max) / max);
}

lv_obj_t *pupu_ui_status_create(const pupu_model_t *m) {
    lv_obj_t *scr = pupu_ui_screen_create();
    pupu_ui_page_frame(scr, PUPU_TXT_MENU_STATUS);
    row_create(scr, ROW_FULL, PUPU_TXT_FULL, true);
    row_create(scr, ROW_HAPPY, PUPU_TXT_HAPPY, true);
    row_create(scr, ROW_CANDY, PUPU_TXT_CANDY, false);
    s_growth = m->stage == PUPU_STAGE_CHILD;
    if (s_growth) row_create(scr, ROW_GROWTH, PUPU_TXT_GROWTH, true);
    pupu_ui_status_update(m);
    return scr;
}

pupu_ui_req_t pupu_ui_status_action(pupu_action_t act) {
    return act == PUPU_ACT_BACK ? PUPU_UI_GO_HOME : PUPU_UI_NONE;
}

void pupu_ui_status_update(const pupu_model_t *m) {
    row_set(ROW_FULL, m->full, PUPU_STAT_MAX);
    row_set(ROW_HAPPY, m->happy, PUPU_STAT_MAX);
    row_set(ROW_CANDY, m->candy, PUPU_CANDY_MAX);
    if (s_growth) row_set(ROW_GROWTH, m->bond, PUPU_ADULT_BOND);
}

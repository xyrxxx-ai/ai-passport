// main/pupu_ui.h —— 噗噗各个页面共用的颜色、布局和页面接口。
//
// 全部只在 LVGL 任务里调用(app 主循环是一个 lv_timer)。同一时间只保留一个页面:
// 进页面时 create 新屏幕,旧屏幕由 pupu_app 删除。界面不用透明度、渐变、变换和切屏动画,
// 这些都会临时从 LVGL 内存池借很多内存。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "pupu_input.h"
#include "pupu_model.h"

#define PUPU_COLOR_BG     0xF4ECDD  // 暖色纸面
#define PUPU_COLOR_PAPER  0xFFFDF7  // 气泡
#define PUPU_COLOR_INK    0x2B2118  // 文字和描边
#define PUPU_COLOR_MUTED  0x8C7F6A  // 没选中的文字
#define PUPU_COLOR_SOFT   0xD8CDB6  // 进度条底色、没选中的圆点
#define PUPU_COLOR_ACCENT 0x2F4A8A  // 护照墨水蓝

// 宠物画布 144×144,水平居中
#define PUPU_PET_X 48
#define PUPU_PET_Y 72

lv_obj_t *pupu_ui_screen_create(void);
lv_obj_t *pupu_ui_label(lv_obj_t *parent, const char *text, uint32_t color);
lv_obj_t *pupu_ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t color, int32_t radius);

// ---- 蛋页:按住 OK 3 秒孵化 ----
lv_obj_t *pupu_ui_egg_create(uint32_t now_ms);
void pupu_ui_egg_action(pupu_action_t act, pupu_model_t *m, uint32_t now_ms);
// 返回 true 表示孵化动画放完了,该进首页。
bool pupu_ui_egg_update(const pupu_input_t *in, uint32_t now_ms);

// ---- 首页:顶栏电量、需求气泡、宠物、底部 4 个功能 ----
lv_obj_t *pupu_ui_home_create(const pupu_model_t *m, uint32_t now_ms);
void pupu_ui_home_action(pupu_action_t act, uint32_t now_ms);
void pupu_ui_home_update(const pupu_model_t *m, uint32_t now_ms);

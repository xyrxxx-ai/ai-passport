// main/pupu_fonts.h —— 噗噗用的字体。
//
// pupu_font_body = 16 px 中文子集(main/fonts/pupu_font_16.c),缺的字形(电池等 LVGL 符号)
// 退回 Montserrat 14。只在 LVGL 任务里使用,或者先拿到 bsp_lvgl_lock()。
#pragma once

#include "lvgl.h"

extern lv_font_t pupu_font_body;

// LVGL 初始化之后、创建任何标签之前调用一次。
void pupu_fonts_init(void);

// 检查 pupu_text.h 里每句话的每个非 ASCII 字在中文子集里都有字形,缺字打印 U+XXXX。
// 返回缺了几个字。
int pupu_fonts_selfcheck(void);

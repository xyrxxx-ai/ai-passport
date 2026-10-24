// main/pupu_pet_view.h —— 噗噗的宠物画布:一块 144×144 的 I4 画布,按 1:1 显示。
//
// 画布的像素放在一块静态缓冲区里(约 10.4 KB,不占 LVGL 内存池)。同一时间只有一个页面,
// 所以只有一个画布对象在用这块缓冲区:换页时新页面重新 create,旧页面随屏幕一起删除。
// 只在 LVGL 任务里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "pupu_assets.h"

// 在 parent 上创建画布,放在 (x, y)。会清掉上一个画布的播放状态。
lv_obj_t *pupu_pet_view_create(lv_obj_t *parent, int32_t x, int32_t y);

// 从第 0 帧开始播放 anim。loop=false 时停在最后一帧。正在播的就是它时什么都不做。
void pupu_pet_view_play(pupu_anim_t anim, bool loop, uint32_t now_ms);

// 主循环每次调用:到时间就换下一帧。只播一次的动画刚播完时返回 true(只返回一次)。
bool pupu_pet_view_tick(uint32_t now_ms);

// 毛色档位 0–7(见 pupu_sprite_math.h)。和现在一样时什么都不做。
void pupu_pet_view_set_tint(int step);

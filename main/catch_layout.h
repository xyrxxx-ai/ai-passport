// main/catch_layout.h —— 小游戏「接星星」的画面布局(纯逻辑,只用标准 C)。
//
// 240×320 竖屏:顶部约 40 px 是剩余秒数和星星数;从 y=56 起是 3 条 80 px 高的跑道,
// 0 号在最上面(↑ 往上换道);噗噗在左边 x 8–56;星星和乌云从右边屏幕外飞进来,
// 中心越过"接取线"的那一帧计分。坐标都是屏幕像素;物体的横坐标在 catch_model 里
// 用 1/16 像素(q4)记,这样每一步可以走 2.625 像素。tests/test_catch_model.c 检查这些数。
#pragma once

#include <stdint.h>

#define CATCH_SCREEN_W   240
#define CATCH_SCREEN_H   320
#define CATCH_HUD_H      40
#define CATCH_LANES      3
#define CATCH_LANE_TOP   56
#define CATCH_LANE_H     80
#define CATCH_PET_X      8
#define CATCH_PET_W      48    // 噗噗用 1 倍大小的帧
#define CATCH_ITEM_W     24    // 星星和乌云的图标
#define CATCH_Q4         16    // 1 像素 = 16 个 q4 单位

// 物体中心的横坐标:出生时整个在屏幕右边外面;越过接取线(在噗噗身体里)计分;
// 整个移出屏幕左边以后空出位置。
#define CATCH_SPAWN_CX   (CATCH_SCREEN_W + CATCH_ITEM_W / 2)
#define CATCH_LINE_CX    44
#define CATCH_GONE_CX    (-CATCH_ITEM_W / 2)

// 屏幕四角是半径 30 的圆角:跑道和顶栏的内容都要躲开。
#define CATCH_CORNER_R   30

static inline int catch_lane_center_y(int lane) {
    return CATCH_LANE_TOP + CATCH_LANE_H * lane + CATCH_LANE_H / 2;
}

static inline int catch_pet_y(int lane) {
    return catch_lane_center_y(lane) - CATCH_PET_W / 2;
}

static inline int catch_item_y(int lane) {
    return catch_lane_center_y(lane) - CATCH_ITEM_W / 2;
}

// q4 中心坐标 → 图标左边的屏幕像素(向下取整,负数也对)。
static inline int catch_item_x(int32_t cx_q4) {
    const int32_t px = cx_q4 >= 0 ? cx_q4 / CATCH_Q4 : -((-cx_q4 + CATCH_Q4 - 1) / CATCH_Q4);
    return (int)px - CATCH_ITEM_W / 2;
}

// main/gate_layout.h —— 小游戏「羊过门」的画面布局(纯逻辑,只用标准 C)。
//
// 240×320 竖屏:顶栏约 40 px 是"羊 N"和"第 k/6 波",下面一条倒计时条;从 y=56 起是 3 条 80 px
// 高的跑道,0 号在最上面(↑ 往上换道);噗噗在左边 x 8–56;每条跑道右边一张静止的卡片
// x 140–220、高 64;时间到,噗噗所在跑道的卡片用 0.3 秒滑到它身边生效。狼群图标 48×48
// 结算时从右边滑进中间跑道。坐标都是屏幕像素。tests/test_gate_model.c 检查这些数。
#pragma once

#define GATE_SCREEN_W    240
#define GATE_SCREEN_H    320
#define GATE_HUD_H       40
#define GATE_BAR_X       20    // 倒计时条
#define GATE_BAR_Y       44
#define GATE_BAR_W       200
#define GATE_BAR_H       6
#define GATE_LANES       3
#define GATE_LANE_TOP    56
#define GATE_LANE_H      80
#define GATE_PET_X       8
#define GATE_PET_W       48    // 噗噗用 1 倍大小的帧
#define GATE_CARD_X      140   // 卡片待着的位置
#define GATE_CARD_W      80
#define GATE_CARD_H      64
#define GATE_CARD_PAD    4     // 卡片文字离左右边至少 4 px
#define GATE_CARD_CELLS  9     // 每行最多 9 格(gate_text_cells)
#define GATE_CELL_PX     8     // 一格 8 px:9 × 8 = 72 = 80 − 2 × 4
#define GATE_CARD_APPLY_X (GATE_PET_X + GATE_PET_W + 4)  // 卡片滑到噗噗身边时的 x
#define GATE_WOLF_W      48
#define GATE_WOLF_X      (GATE_SCREEN_W - GATE_WOLF_W - 24)  // 狼群滑进来停在哪

// 屏幕四角是半径 30 的圆角:跑道和顶栏的内容都要躲开。
#define GATE_CORNER_R    30

static inline int gate_lane_top(int lane) {
    return GATE_LANE_TOP + GATE_LANE_H * lane;
}

static inline int gate_lane_center_y(int lane) {
    return gate_lane_top(lane) + GATE_LANE_H / 2;
}

static inline int gate_pet_y(int lane) {
    return gate_lane_center_y(lane) - GATE_PET_W / 2;
}

static inline int gate_card_y(int lane) {
    return gate_lane_top(lane) + (GATE_LANE_H - GATE_CARD_H) / 2;
}

// 卡片滑向噗噗:第 step 步(0..total)的左边 x,匀速。
static inline int gate_card_slide_x(int step, int total) {
    return GATE_CARD_X - (GATE_CARD_X - GATE_CARD_APPLY_X) * step / total;
}

// 狼群滑进来:第 step 步(0..total)的左边 x,从屏幕右边外面到 GATE_WOLF_X。
static inline int gate_wolf_slide_x(int step, int total) {
    return GATE_SCREEN_W - (GATE_SCREEN_W - GATE_WOLF_X) * step / total;
}

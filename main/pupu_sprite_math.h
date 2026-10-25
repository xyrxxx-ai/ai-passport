// main/pupu_sprite_math.h —— 噗噗像素图的纯计算部分(纯逻辑,只用标准 C)。
//
// 每帧是 48×48 的 4 位色号图(I4):一行 24 字节,偶数 x 放在高 4 位,和 LVGL 的
// LV_COLOR_FORMAT_I4 一致。16 个色号共用一张调色板(assets/images/pupu/pupu.gpl,
// 生成到 pupu_assets.c),每个色号有固定用途,其中 8–10 号是羊毛,换毛色时只改这 3 个色。
//
// 屏幕上用一块 144×144 的 I4 画布按 1:1 显示:换帧时放大 3 倍拷进去,不在运行时缩放。
// M2 真机试验测过:这样每帧约 26 ms,拉伸 RGB565A8 的备选方案约 71 ms。
//
// 颜色统一写成 0xAARRGGBB(A=0 表示透明)。转成 LVGL 颜色由调用方负责。
#pragma once

#include <stdint.h>

#define PUPU_SPRITE_W           48
#define PUPU_SPRITE_ROW_BYTES   (PUPU_SPRITE_W / 2)
#define PUPU_SPRITE_FRAME_BYTES (PUPU_SPRITE_ROW_BYTES * PUPU_SPRITE_W)
#define PUPU_SPRITE_SCALE       3
#define PUPU_SPRITE_BIG_W       (PUPU_SPRITE_W * PUPU_SPRITE_SCALE)
#define PUPU_SPRITE_BIG_ROW_BYTES (PUPU_SPRITE_BIG_W / 2)

#define PUPU_PALETTE_SIZE 16

// 色号的用途。改动时要同步改 assets/images/pupu/pupu.gpl 和那里的 README。
enum {
    PUPU_SLOT_CLEAR = 0,
    PUPU_SLOT_OUTLINE = 1,
    PUPU_SLOT_FACE = 2,
    PUPU_SLOT_FACE_HI = 3,
    PUPU_SLOT_EYE = 4,
    PUPU_SLOT_BLUSH = 5,
    PUPU_SLOT_HORN = 6,
    PUPU_SLOT_HORN_DARK = 7,
    PUPU_SLOT_WOOL_LIGHT = 8,
    PUPU_SLOT_WOOL_MID = 9,
    PUPU_SLOT_WOOL_DARK = 10,
    PUPU_SLOT_SHELL = 11,
    PUPU_SLOT_SHELL_DARK = 12,
    PUPU_SLOT_INK = 13,
    PUPU_SLOT_STAR = 14,   // 金色:被砸晕时头顶转的小星星
    PUPU_SLOT_CLOUD = 15,  // 乌云灰:emo 时头顶的小乌云
};

#define PUPU_TINT_STEPS 8  // 0 = 最开心的奶油色,7 = 最灰

// 毛色档位:看饱腹和开心里较低的那个,每 13 点一档。100–88 是第 0 档,9–0 是第 7 档。
int pupu_tint_step(int full, int happy);

// 第 step 档的羊毛颜色:从奶油色 cream(调色板里的颜色)按档位混向灰色。
// shade:0 亮、1 中、2 暗。step、shade 超出范围时按边界处理。
uint32_t pupu_tint_wool(uint32_t cream, int shade, int step);

// 第 step 档的完整调色板:复制 base,只替换 8–10 号羊毛色。
void pupu_tint_palette(const uint32_t base[PUPU_PALETTE_SIZE], int step,
                       uint32_t out[PUPU_PALETTE_SIZE]);

// 读一个像素的色号。
uint8_t pupu_sprite_px(const uint8_t *frame, int x, int y);

// 把 48×48 的帧放大 3 倍,写成 144×144 的 I4 像素(dst 指向第 0 行,每行跨 dst_stride 字节)。
void pupu_sprite_expand3(const uint8_t *frame, uint8_t *dst, uint32_t dst_stride);

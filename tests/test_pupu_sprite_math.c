#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "pupu_sprite_math.h"

static uint8_t big_px(const uint8_t *big, uint32_t stride, int x, int y) {
    const uint8_t b = big[(uint32_t)y * stride + (uint32_t)x / 2];
    return (x & 1) ? (uint8_t)(b & 0x0Fu) : (uint8_t)(b >> 4);
}

static void fill_pseudo_random(uint8_t *frame) {
    uint32_t seed = 12345u;
    for (int i = 0; i < PUPU_SPRITE_FRAME_BYTES; i++) {
        seed = seed * 1103515245u + 12345u;
        frame[i] = (uint8_t)(seed >> 16);
    }
}

static void test_tint_step_bins(void) {
    assert(pupu_tint_step(100, 100) == 0);
    assert(pupu_tint_step(88, 100) == 0);
    assert(pupu_tint_step(87, 100) == 1);
    assert(pupu_tint_step(75, 90) == 1);
    assert(pupu_tint_step(74, 90) == 2);
    assert(pupu_tint_step(10, 50) == 6);   // 补算最低 10:最多到第 6 档
    assert(pupu_tint_step(9, 50) == 7);
    assert(pupu_tint_step(0, 0) == 7);
    // 看较低的那个
    assert(pupu_tint_step(100, 30) == pupu_tint_step(30, 100));
    assert(pupu_tint_step(100, 30) == 5);
    // 超出范围按边界处理
    assert(pupu_tint_step(150, 150) == 0);
    assert(pupu_tint_step(-5, 80) == 7);
    // 档位只会随数值变低而变灰,不会跳回去
    int prev = 0;
    for (int v = 100; v >= 0; v--) {
        int step = pupu_tint_step(v, 100);
        assert(step >= prev && step <= prev + 1);
        prev = step;
    }
    assert(prev == PUPU_TINT_STEPS - 1);
}

// 调色板里羊毛的奶油色(和 assets/images/pupu/pupu.gpl 一致即可,测试只关心规律)
static const uint32_t k_cream[3] = { 0xFFFFF8E7u, 0xFFF1E0BEu, 0xFFD6BD93u };

static int channel(uint32_t c, int shift) {
    return (int)((c >> shift) & 0xFFu);
}

static void test_tint_wool_ends_and_order(void) {
    for (int shade = 0; shade < 3; shade++) {
        // 第 0 档就是调色板里的奶油色
        assert(pupu_tint_wool(k_cream[shade], shade, 0) == k_cream[shade]);
        for (int step = 1; step < PUPU_TINT_STEPS; step++) {
            uint32_t c = pupu_tint_wool(k_cream[shade], shade, step);
            uint32_t p = pupu_tint_wool(k_cream[shade], shade, step - 1);
            uint32_t grey = pupu_tint_wool(k_cream[shade], shade, PUPU_TINT_STEPS - 1);
            assert((c >> 24) == 0xFFu);  // 羊毛永远不透明
            assert(c != p);
            // 每一档的每个通道都单调地从奶油色走向灰色
            for (int s = 0; s <= 16; s += 8) {
                int dir = channel(grey, s) - channel(k_cream[shade], s);
                int d = channel(c, s) - channel(p, s);
                assert(dir >= 0 ? d >= 0 : d <= 0);
            }
        }
    }
    // 亮、中、暗三档在每个毛色档里都保持明暗顺序
    for (int step = 0; step < PUPU_TINT_STEPS; step++) {
        uint32_t light = pupu_tint_wool(k_cream[0], 0, step);
        uint32_t mid = pupu_tint_wool(k_cream[1], 1, step);
        uint32_t dark = pupu_tint_wool(k_cream[2], 2, step);
        for (int s = 0; s <= 16; s += 8) {
            assert(channel(light, s) >= channel(mid, s));
            assert(channel(mid, s) >= channel(dark, s));
        }
    }
    // 最灰的一档:三个通道接近相等(真的是灰色,不是偏黄),而且和奶油色无关
    uint32_t g = pupu_tint_wool(k_cream[1], 1, PUPU_TINT_STEPS - 1);
    assert(channel(g, 16) - channel(g, 0) <= 8);
    assert(g == pupu_tint_wool(0xFF123456u, 1, PUPU_TINT_STEPS - 1));
    // 超出范围按边界处理
    assert(pupu_tint_wool(k_cream[1], 1, -1) == k_cream[1]);
    assert(pupu_tint_wool(k_cream[1], 1, 99) == g);
    assert(pupu_tint_wool(k_cream[2], 9, 3) == pupu_tint_wool(k_cream[2], 2, 3));
}

static void test_tint_palette_only_touches_wool(void) {
    uint32_t base[PUPU_PALETTE_SIZE];
    for (int i = 0; i < PUPU_PALETTE_SIZE; i++) base[i] = 0xFF000000u | (uint32_t)(i * 0x111111);
    base[PUPU_SLOT_CLEAR] = 0;
    base[PUPU_SLOT_WOOL_LIGHT] = k_cream[0];
    base[PUPU_SLOT_WOOL_MID] = k_cream[1];
    base[PUPU_SLOT_WOOL_DARK] = k_cream[2];
    for (int step = 0; step < PUPU_TINT_STEPS; step++) {
        uint32_t out[PUPU_PALETTE_SIZE];
        pupu_tint_palette(base, step, out);
        for (int i = 0; i < PUPU_PALETTE_SIZE; i++) {
            if (i >= PUPU_SLOT_WOOL_LIGHT && i <= PUPU_SLOT_WOOL_DARK) {
                assert(out[i] == pupu_tint_wool(base[i], i - PUPU_SLOT_WOOL_LIGHT, step));
            } else {
                // 黑脸、眼睛、透明色等都不变,换毛色时黑脸不会被染灰
                assert(out[i] == base[i]);
            }
        }
    }
}

static void test_px_order_matches_lvgl_i4(void) {
    // LVGL I4:偶数 x 在高 4 位(lv_canvas.c 里 shift = 4 - 4 * (x & 1))
    uint8_t frame[PUPU_SPRITE_FRAME_BYTES];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAB;
    frame[PUPU_SPRITE_ROW_BYTES * 2 + 23] = 0x5C;
    assert(pupu_sprite_px(frame, 0, 0) == 0xA);
    assert(pupu_sprite_px(frame, 1, 0) == 0xB);
    assert(pupu_sprite_px(frame, 46, 2) == 0x5);
    assert(pupu_sprite_px(frame, 47, 2) == 0xC);
    assert(pupu_sprite_px(frame, 2, 0) == 0);
}

static void check_expand(uint32_t stride) {
    uint8_t frame[PUPU_SPRITE_FRAME_BYTES];
    fill_pseudo_random(frame);
    static uint8_t big[160 * PUPU_SPRITE_BIG_W];
    assert(stride * PUPU_SPRITE_BIG_W <= sizeof(big));
    memset(big, 0xEE, sizeof(big));
    pupu_sprite_expand3(frame, big, stride);
    for (int y = 0; y < PUPU_SPRITE_BIG_W; y++) {
        for (int x = 0; x < PUPU_SPRITE_BIG_W; x++) {
            assert(big_px(big, stride, x, y) == pupu_sprite_px(frame, x / 3, y / 3));
        }
        // 每行末尾的填充字节不能被改动
        for (uint32_t i = PUPU_SPRITE_BIG_ROW_BYTES; i < stride; i++) {
            assert(big[(uint32_t)y * stride + i] == 0xEE);
        }
    }
    // 最后一行之后也不能写出界
    assert(big[stride * PUPU_SPRITE_BIG_W] == 0xEE);
}

static void test_expand3_is_pixel_exact(void) {
    check_expand(PUPU_SPRITE_BIG_ROW_BYTES);  // 设备上用的紧密排列
    check_expand(PUPU_SPRITE_BIG_ROW_BYTES + 8);
}

int main(void) {
    test_tint_step_bins();
    test_tint_wool_ends_and_order();
    test_tint_palette_only_touches_wool();
    test_px_order_matches_lvgl_i4();
    test_expand3_is_pixel_exact();
    return 0;
}

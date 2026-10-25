#include "pupu_sprite_math.h"

// 第 7 档(最灰)的羊毛:亮、中、暗。第 0 档就是调色板里的奶油色。
static const uint32_t k_wool_grey[3] = { 0xFFC4C2BEu, 0xFFA3A19Du, 0xFF7F7D79u };

int pupu_tint_step(int full, int happy) {
    int low = full < happy ? full : happy;
    if (low > 100) low = 100;
    if (low < 0) low = 0;
    int step = (100 - low) / 13;
    return step > PUPU_TINT_STEPS - 1 ? PUPU_TINT_STEPS - 1 : step;
}

static uint32_t blend_channel(uint32_t a, uint32_t b, int shift, int step) {
    const int last = PUPU_TINT_STEPS - 1;
    int ca = (int)((a >> shift) & 0xFFu);
    int cb = (int)((b >> shift) & 0xFFu);
    int c = (ca * (last - step) + cb * step + last / 2) / last;
    return (uint32_t)c << shift;
}

uint32_t pupu_tint_wool(uint32_t cream, int shade, int step) {
    if (step < 0) step = 0;
    if (step > PUPU_TINT_STEPS - 1) step = PUPU_TINT_STEPS - 1;
    if (shade < 0) shade = 0;
    if (shade > 2) shade = 2;
    const uint32_t grey = k_wool_grey[shade];
    return 0xFF000000u | blend_channel(cream, grey, 16, step) |
           blend_channel(cream, grey, 8, step) | blend_channel(cream, grey, 0, step);
}

void pupu_tint_palette(const uint32_t base[PUPU_PALETTE_SIZE], int step,
                       uint32_t out[PUPU_PALETTE_SIZE]) {
    for (int i = 0; i < PUPU_PALETTE_SIZE; i++) out[i] = base[i];
    for (int shade = 0; shade < 3; shade++) {
        const int slot = PUPU_SLOT_WOOL_LIGHT + shade;
        out[slot] = pupu_tint_wool(base[slot], shade, step);
    }
}

uint8_t pupu_sprite_px(const uint8_t *frame, int x, int y) {
    const uint8_t b = frame[y * PUPU_SPRITE_ROW_BYTES + x / 2];
    return (x & 1) ? (uint8_t)(b & 0x0Fu) : (uint8_t)(b >> 4);
}

void pupu_sprite_expand3(const uint8_t *frame, uint8_t *dst, uint32_t dst_stride) {
    for (int y = 0; y < PUPU_SPRITE_W; y++) {
        uint8_t *row = dst + (uint32_t)(y * PUPU_SPRITE_SCALE) * dst_stride;
        // 一个源像素变成 3 个目标像素;两个源像素正好是 6 个目标像素 = 3 个字节。
        for (int x = 0; x < PUPU_SPRITE_W; x += 2) {
            const uint8_t a = pupu_sprite_px(frame, x, y);
            const uint8_t b = pupu_sprite_px(frame, x + 1, y);
            uint8_t *out = row + (x / 2) * 3;
            out[0] = (uint8_t)((a << 4) | a);
            out[1] = (uint8_t)((a << 4) | b);
            out[2] = (uint8_t)((b << 4) | b);
        }
        for (int copy = 1; copy < PUPU_SPRITE_SCALE; copy++) {
            uint8_t *next = row + (uint32_t)copy * dst_stride;
            for (int i = 0; i < PUPU_SPRITE_BIG_ROW_BYTES; i++) next[i] = row[i];
        }
    }
}

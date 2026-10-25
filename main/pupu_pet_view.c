#include "pupu_pet_view.h"

#include "pupu_sprite_math.h"

// 16 色调色板(64 字节)+ 144 行 × 72 字节。LVGL 的 I4 缓冲区把调色板放在像素前面。
static LV_ATTRIBUTE_MEM_ALIGN uint8_t s_buf[PUPU_PALETTE_SIZE * 4 +
                                            PUPU_SPRITE_BIG_ROW_BYTES * PUPU_SPRITE_BIG_W];
static lv_draw_buf_t s_draw_buf;
static lv_obj_t *s_canvas;

static int s_anim = -1;
static bool s_loop;
static bool s_done;
static uint8_t s_index;  // 动画里的第几帧
static uint32_t s_frame_start_ms;
static int s_tint = -1;

static lv_color32_t to_lv32(uint32_t argb) {
    return lv_color32_make((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                           (uint8_t)(argb >> 24));
}

static void show_frame(void) {
    const pupu_anim_info_t *info = &pupu_anims[s_anim];
    uint8_t *px = (uint8_t *)lv_draw_buf_goto_xy(&s_draw_buf, 0, 0);
    pupu_sprite_expand3(pupu_frames[info->first + s_index], px, s_draw_buf.header.stride);
    lv_obj_invalidate(s_canvas);
}

lv_obj_t *pupu_pet_view_create(lv_obj_t *parent, int32_t x, int32_t y) {
    // 不用 lv_canvas_set_buffer:它按 stride × h 算大小,漏掉了 I4 前面的调色板。
    lv_draw_buf_init(&s_draw_buf, PUPU_SPRITE_BIG_W, PUPU_SPRITE_BIG_W, LV_COLOR_FORMAT_I4,
                     PUPU_SPRITE_BIG_ROW_BYTES, s_buf, sizeof(s_buf));
    for (int i = 0; i < PUPU_PALETTE_SIZE; i++) {
        lv_draw_buf_set_palette(&s_draw_buf, (uint8_t)i, to_lv32(pupu_asset_palette[i]));
    }
    s_canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(s_canvas, &s_draw_buf);
    lv_obj_set_pos(s_canvas, x, y);
    s_anim = -1;
    s_tint = 0;  // 调色板刚恢复成奶油色
    s_done = false;
    return s_canvas;
}

void pupu_pet_view_play(pupu_anim_t anim, bool loop, uint32_t now_ms) {
    if (s_canvas == NULL || (int)anim == s_anim) return;
    s_anim = (int)anim;
    s_loop = loop;
    s_done = false;
    s_index = 0;
    s_frame_start_ms = now_ms;
    show_frame();
}

bool pupu_pet_view_tick(uint32_t now_ms) {
    if (s_canvas == NULL || s_anim < 0 || s_done) return false;
    const pupu_anim_info_t *info = &pupu_anims[s_anim];
    if (now_ms - s_frame_start_ms < pupu_frame_ms[info->first + s_index]) return false;
    s_frame_start_ms = now_ms;
    if (s_index + 1 < info->count) {
        s_index++;
    } else if (s_loop) {
        s_index = 0;
    } else {
        s_done = true;  // 停在最后一帧
        return true;
    }
    if (info->count > 1) show_frame();
    return false;
}

void pupu_pet_view_set_tint(int step) {
    if (s_canvas == NULL || step == s_tint) return;
    s_tint = step;
    uint32_t palette[PUPU_PALETTE_SIZE];
    pupu_tint_palette(pupu_asset_palette, step, palette);
    for (int slot = PUPU_SLOT_WOOL_LIGHT; slot <= PUPU_SLOT_WOOL_DARK; slot++) {
        // lv_canvas_set_palette 会自己让画布重画
        lv_canvas_set_palette(s_canvas, (uint8_t)slot, to_lv32(palette[slot]));
    }
}

#include "pupu_fonts.h"

#include "pupu_text.h"
#include "esp_log.h"

#include <inttypes.h>

LV_FONT_DECLARE(pupu_font_16);

static const char *TAG = "pupu_fonts";

lv_font_t pupu_font_body;

void pupu_fonts_init(void) {
    // 生成的字库是只读的,复制一份可写的描述再挂后备字体(见 lvgl-chinese-fonts.md 第 5 节)。
    pupu_font_body = pupu_font_16;
    pupu_font_body.fallback = &lv_font_montserrat_14;
}

#define PUPU_TEXT_ITEM(s) s,
static const char *const k_texts[] = { PUPU_TEXT_LIST(PUPU_TEXT_ITEM) };

// 读出 *p 处的一个 UTF-8 字符并前进;界面文字都是编译期写死的合法 UTF-8。
static uint32_t next_codepoint(const char **p) {
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp;
    int extra;
    if (s[0] < 0x80) {
        cp = s[0];
        extra = 0;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1Fu;
        extra = 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0Fu;
        extra = 2;
    } else {
        cp = s[0] & 0x07u;
        extra = 3;
    }
    for (int i = 1; i <= extra && s[i] != 0; i++) cp = (cp << 6) | (s[i] & 0x3Fu);
    *p += 1 + extra;
    return cp;
}

int pupu_fonts_selfcheck(void) {
    int missing = 0;
    int checked = 0;
    for (size_t i = 0; i < sizeof(k_texts) / sizeof(k_texts[0]); i++) {
        const char *p = k_texts[i];
        while (*p != '\0') {
            const uint32_t cp = next_codepoint(&p);
            if (cp <= 0x7E) continue;
            lv_font_glyph_dsc_t glyph = {0};
            // 只查中文子集本身,不算后备字体:Montserrat 没有中文,算进去会掩盖缺字。
            if (!lv_font_get_glyph_dsc(&pupu_font_16, &glyph, cp, 0) || glyph.is_placeholder) {
                ESP_LOGE(TAG, "缺字 U+%04" PRIX32 ",在 \"%s\" 里", cp, k_texts[i]);
                missing++;
            }
            checked++;
        }
    }
    if (missing == 0) {
        ESP_LOGI(TAG, "字库自检通过:%d 个非 ASCII 字都有字形", checked);
    }
    return missing;
}

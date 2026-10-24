#include <assert.h>
#include <stdint.h>

#include "pupu_config.h"
#include "pupu_input.h"

#define MENU PUPU_INPUT_MENU
#define EGG PUPU_INPUT_EGG

static pupu_action_t key(pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t k,
                         pupu_key_ev_t ev, uint32_t t) {
    return pupu_input_key(in, mode, k, ev, t);
}

// 一次完整的短按:PRESS → RELEASE → 约 185 ms 后 CLICK。返回 PRESS 和 CLICK 的动作。
static void tap(pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t k, uint32_t t,
                pupu_action_t *on_press, pupu_action_t *on_click) {
    *on_press = key(in, mode, k, PUPU_KEV_PRESS, t);
    assert(key(in, mode, k, PUPU_KEV_RELEASE, t + 80) == PUPU_ACT_NONE);
    *on_click = key(in, mode, k, PUPU_KEV_CLICK, t + 80 + 185);
}

static void test_menu_mapping(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    pupu_action_t p, c;
    tap(&in, MENU, PUPU_KEY_UP, 0, &p, &c);
    assert(p == PUPU_ACT_PREV && c == PUPU_ACT_NONE);  // ↑/↓ 只认 PRESS
    tap(&in, MENU, PUPU_KEY_DOWN, 1000, &p, &c);
    assert(p == PUPU_ACT_NEXT && c == PUPU_ACT_NONE);
    tap(&in, MENU, PUPU_KEY_OK, 2000, &p, &c);
    assert(p == PUPU_ACT_NONE && c == PUPU_ACT_CONFIRM);  // OK 等单击

    // 快速按两下只有一个 DOUBLE,当一次确认
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_PRESS, 3000) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 3060) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_PRESS, 3120) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 3180) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_DOUBLE, 3365) == PUPU_ACT_CONFIRM);

    // 长按 OK = 返回;松开后不会再有单击
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_PRESS, 4000) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_LONG, 4500) == PUPU_ACT_BACK);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 5200) == PUPU_ACT_NONE);

    // ↑/↓ 的长按和双击没有动作
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_PRESS, 6000) == PUPU_ACT_PREV);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_LONG, 6500) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_RELEASE, 6600) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_DOUBLE, 6800) == PUPU_ACT_NONE);

    // 不认识的键编号不会越界
    assert(key(&in, MENU, (pupu_key_t)7, PUPU_KEV_PRESS, 7000) == PUPU_ACT_NONE);
}

static void test_phantom_key_while_another_is_held(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    // 按住 OK 时 ADC 误读出 ↓:它的 PRESS、RELEASE、CLICK 全部忽略
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_PRESS, 300) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_RELEASE, 320) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_CLICK, 505) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_LONG, 510) == PUPU_ACT_NONE);
    // OK 自己的松开和单击照常
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 400) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_CLICK, 585) == PUPU_ACT_CONFIRM);
}

static void test_phantom_key_right_after_release(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    // 按着 ↑ 时接触不良:驱动先报 ↑ 松开,紧接着报 ↓ 按下
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_PRESS, 0) == PUPU_ACT_PREV);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_RELEASE, 600) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_PRESS, 600 + PUPU_KEY_SWITCH_MS - 1) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_RELEASE, 740) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_CLICK, 925) == PUPU_ACT_NONE);
    // 过了这段时间,换键就是真的
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_PRESS, 2000) == PUPU_ACT_NEXT);
    assert(key(&in, MENU, PUPU_KEY_DOWN, PUPU_KEV_RELEASE, 2080) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_PRESS, 2080 + PUPU_KEY_SWITCH_MS) == PUPU_ACT_PREV);
    // 同一个键松开后马上又按(连按 ↓ 翻页)不算幻影键
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_RELEASE, 2300) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_PRESS, 2340) == PUPU_ACT_PREV);
}

static void test_late_click_still_counts_after_switching_keys(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    // 按完 OK 很快又按 ↑:OK 的单击约 185 ms 后才到,仍然是一次确认
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 80) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_PRESS, 80 + PUPU_KEY_SWITCH_MS) == PUPU_ACT_PREV);
    assert(key(&in, MENU, PUPU_KEY_UP, PUPU_KEV_RELEASE, 230) == PUPU_ACT_NONE);
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_CLICK, 265) == PUPU_ACT_CONFIRM);
}

static void test_egg_hatches_after_three_seconds(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(pupu_input_hold_ms(&in, 0) == 0);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 1000) == PUPU_ACT_HOLD_START);
    assert(pupu_input_tick(&in, 1500) == PUPU_ACT_NONE);
    // 驱动在 500 ms 发的长按,蛋页不理(不会熄屏、不会返回)
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_LONG, 1500) == PUPU_ACT_NONE);
    assert(pupu_input_hold_ms(&in, 2500) == 1500);
    assert(pupu_input_tick(&in, 1000 + PUPU_HOLD_MS - 1) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 1000 + PUPU_HOLD_MS) == PUPU_ACT_HATCH);
    // 只孵化一次
    assert(pupu_input_hold_ms(&in, 4100) == 0);
    assert(pupu_input_tick(&in, 4200) == PUPU_ACT_NONE);
    // 孵化后已经转到首页,手才松开:长按之后驱动只发 RELEASE,不会多出一次确认
    assert(key(&in, MENU, PUPU_KEY_OK, PUPU_KEV_RELEASE, 4300) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 5000) == PUPU_ACT_NONE);
}

static void test_egg_release_cancels_after_grace(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_HOLD_START);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_RELEASE, 1000) == PUPU_ACT_NONE);
    // 松开后进度停在松手那一刻
    assert(pupu_input_hold_ms(&in, 1100) == 1000);
    assert(pupu_input_tick(&in, 1000 + PUPU_HOLD_GRACE_MS - 1) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 1000 + PUPU_HOLD_GRACE_MS) == PUPU_ACT_HOLD_CANCEL);
    assert(pupu_input_hold_ms(&in, 1200) == 0);
    assert(pupu_input_tick(&in, 4000) == PUPU_ACT_NONE);
    // 短按留下的单击、双击在蛋页没有动作
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_CLICK, 1185) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_DOUBLE, 1190) == PUPU_ACT_NONE);
}

static void test_egg_contact_bounce_does_not_cancel(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_HOLD_START);
    // 按住时接触抖了一下:驱动报一次假的松开和按下
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_RELEASE, 1000) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 1000 + PUPU_HOLD_GRACE_MS - 1) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 1200) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, PUPU_HOLD_MS) == PUPU_ACT_HATCH);
}

static void test_egg_needs_the_key_still_down_at_three_seconds(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_HOLD_START);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_RELEASE, PUPU_HOLD_MS - 50) == PUPU_ACT_NONE);
    // 已经过了 3 秒,但手在 3 秒前就松开了:不孵化,宽限期过后取消
    assert(pupu_input_tick(&in, PUPU_HOLD_MS) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, PUPU_HOLD_MS - 50 + PUPU_HOLD_GRACE_MS) == PUPU_ACT_HOLD_CANCEL);
}

static void test_egg_tap_then_hold(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    // 先点一下再按住:驱动不会发长按,但计时只看 PRESS 和 RELEASE,照样能孵化
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_HOLD_START);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_RELEASE, 90) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 90 + PUPU_HOLD_GRACE_MS) == PUPU_ACT_HOLD_CANCEL);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 300) == PUPU_ACT_HOLD_START);
    assert(pupu_input_tick(&in, 300 + PUPU_HOLD_MS) == PUPU_ACT_HATCH);
}

static void test_egg_ignores_arrows_and_phantoms(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(key(&in, EGG, PUPU_KEY_UP, PUPU_KEV_PRESS, 0) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_UP, PUPU_KEV_RELEASE, 80) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_DOWN, PUPU_KEV_PRESS, 1000) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_DOWN, PUPU_KEV_RELEASE, 1080) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 2000) == PUPU_ACT_NONE);
    // 按住 OK 时误读出 ↑:不影响捂热
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 3000) == PUPU_ACT_HOLD_START);
    assert(key(&in, EGG, PUPU_KEY_UP, PUPU_KEV_PRESS, 3500) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_UP, PUPU_KEV_RELEASE, 3510) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, 3000 + PUPU_HOLD_MS) == PUPU_ACT_HATCH);
}

static void test_cancel_hold_when_leaving(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, 0) == PUPU_ACT_HOLD_START);
    pupu_input_cancel_hold(&in);
    assert(pupu_input_hold_ms(&in, 1000) == 0);
    assert(pupu_input_tick(&in, PUPU_HOLD_MS + 10) == PUPU_ACT_NONE);
}

static void test_millisecond_wraparound(void) {
    pupu_input_t in;
    pupu_input_init(&in);
    const uint32_t start = UINT32_MAX - 1000u;
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, start) == PUPU_ACT_HOLD_START);
    assert(pupu_input_hold_ms(&in, start + 2000u) == 2000);  // 计时器回绕到 0 附近
    assert(pupu_input_tick(&in, start + 2999u) == PUPU_ACT_NONE);
    assert(pupu_input_tick(&in, start + 3000u) == PUPU_ACT_HATCH);
    // 进度条不会超过 3 秒
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_RELEASE, start + 3100u) == PUPU_ACT_NONE);
    assert(key(&in, EGG, PUPU_KEY_OK, PUPU_KEV_PRESS, start + 5000u) == PUPU_ACT_HOLD_START);
    assert(pupu_input_hold_ms(&in, start + 5000u + 9999u) == PUPU_HOLD_MS);
}

int main(void) {
    test_menu_mapping();
    test_phantom_key_while_another_is_held();
    test_phantom_key_right_after_release();
    test_late_click_still_counts_after_switching_keys();
    test_egg_hatches_after_three_seconds();
    test_egg_release_cancels_after_grace();
    test_egg_contact_bounce_does_not_cancel();
    test_egg_needs_the_key_still_down_at_three_seconds();
    test_egg_tap_then_hold();
    test_egg_ignores_arrows_and_phantoms();
    test_cancel_hold_when_leaving();
    test_millisecond_wraparound();
    return 0;
}

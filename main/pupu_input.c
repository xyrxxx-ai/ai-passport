#include "pupu_input.h"

#include "pupu_config.h"

void pupu_input_init(pupu_input_t *in) {
    *in = (pupu_input_t){ .down = -1, .last = -1 };
}

// 另一个键还按着,或者刚松开不久:这个 PRESS 很可能是 ADC 读错了。
// 小游戏里 ↑ 和 ↓ 之间快速交替是正常操作,只对 OK 用"刚松开"这条。
static bool is_phantom_press(const pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t key,
                             uint32_t now_ms) {
    if (in->down >= 0) return in->down != (int8_t)key;
    if (mode == PUPU_INPUT_GAME && key != PUPU_KEY_OK) return false;
    return in->any_released && now_ms - in->released_ms < PUPU_KEY_SWITCH_MS &&
           in->last != (int8_t)key;
}

static pupu_action_t on_press(pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t key,
                              uint32_t now_ms) {
    if (is_phantom_press(in, mode, key, now_ms)) {
        in->accepted[key] = false;
        return PUPU_ACT_NONE;
    }
    // 别的键之前的 PRESS 仍然有效:按完 OK 很快又按 ↑ 时,OK 迟到的单击还要算数。
    in->down = (int8_t)key;
    in->last = (int8_t)key;
    in->accepted[key] = true;

    if (mode == PUPU_INPUT_EGG) {
        if (key != PUPU_KEY_OK) return PUPU_ACT_NONE;
        if (in->holding) {
            in->release_pending = false;  // 宽限期内又按下:接触抖动,继续计时
            return PUPU_ACT_NONE;
        }
        in->holding = true;
        in->release_pending = false;
        in->hold_start_ms = now_ms;
        return PUPU_ACT_HOLD_START;
    }
    if (key == PUPU_KEY_UP) return PUPU_ACT_PREV;
    if (key == PUPU_KEY_DOWN) return PUPU_ACT_NEXT;
    if (mode != PUPU_INPUT_GAME) return PUPU_ACT_NONE;
    in->game_ok_held = true;
    in->game_ok_ms = now_ms;
    return PUPU_ACT_OK_DOWN;
}

pupu_action_t pupu_input_key(pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t key,
                             pupu_key_ev_t ev, uint32_t now_ms) {
    if ((int)key < 0 || key >= PUPU_KEY_COUNT) return PUPU_ACT_NONE;
    switch (ev) {
    case PUPU_KEV_PRESS:
        return on_press(in, mode, key, now_ms);
    case PUPU_KEV_RELEASE:
        if (in->down != (int8_t)key) return PUPU_ACT_NONE;  // 幻影键自己的松开
        in->down = -1;
        in->released_ms = now_ms;
        in->any_released = true;
        if (mode == PUPU_INPUT_EGG && key == PUPU_KEY_OK && in->holding) {
            in->release_pending = true;
            in->hold_release_ms = now_ms;
        }
        if (key == PUPU_KEY_OK) in->game_ok_held = false;
        return mode == PUPU_INPUT_GAME && key == PUPU_KEY_OK ? PUPU_ACT_OK_UP : PUPU_ACT_NONE;
    case PUPU_KEV_CLICK:
    case PUPU_KEV_DOUBLE:
        if (!in->accepted[key] || (in->down >= 0 && in->down != (int8_t)key)) return PUPU_ACT_NONE;
        if (mode == PUPU_INPUT_MENU && key == PUPU_KEY_OK) return PUPU_ACT_CONFIRM;
        return PUPU_ACT_NONE;
    case PUPU_KEV_LONG:
        if (!in->accepted[key] || in->down != (int8_t)key) return PUPU_ACT_NONE;
        // 蛋页不响应长按:它在 500 ms 就会触发,而孵化要按满 3 秒。
        // 小游戏自己数 500 ms(见 pupu_input_tick),驱动的长按不再算一次。
        if (mode == PUPU_INPUT_MENU && key == PUPU_KEY_OK) return PUPU_ACT_BACK;
        return PUPU_ACT_NONE;
    }
    return PUPU_ACT_NONE;
}

pupu_action_t pupu_input_tick(pupu_input_t *in, uint32_t now_ms) {
    if (in->game_ok_held && in->down == PUPU_KEY_OK && now_ms - in->game_ok_ms >= PUPU_GAME_EXIT_MS) {
        in->game_ok_held = false;
        // 退出后回到普通页面:这次按下后面迟到的驱动长按不能再算一次"返回"
        in->accepted[PUPU_KEY_OK] = false;
        return PUPU_ACT_BACK;
    }
    if (!in->holding) return PUPU_ACT_NONE;
    if (in->release_pending) {
        if (now_ms - in->hold_release_ms >= PUPU_HOLD_GRACE_MS) {
            pupu_input_cancel_hold(in);
            return PUPU_ACT_HOLD_CANCEL;
        }
        return PUPU_ACT_NONE;  // 松开了还在宽限期里:先不判孵化,也不判取消
    }
    if (now_ms - in->hold_start_ms >= PUPU_HOLD_MS) {
        pupu_input_cancel_hold(in);
        return PUPU_ACT_HATCH;
    }
    return PUPU_ACT_NONE;
}

uint32_t pupu_input_hold_ms(const pupu_input_t *in, uint32_t now_ms) {
    if (!in->holding) return 0;
    const uint32_t end = in->release_pending ? in->hold_release_ms : now_ms;
    const uint32_t held = end - in->hold_start_ms;
    return held > PUPU_HOLD_MS ? PUPU_HOLD_MS : held;
}

void pupu_input_cancel_hold(pupu_input_t *in) {
    in->holding = false;
    in->release_pending = false;
    in->game_ok_held = false;
}

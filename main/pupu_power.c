#include "pupu_power.h"

#include "pupu_config.h"

// 有符号比较:按键事件的时间可能比主循环这一圈读到的 now 还晚一点(主循环在处理按键时,
// 按键回调又放进来一个),这时算"还没到",不能回绕成一个超大的数。间隔都远小于 2^31 ms。
static bool reached(uint32_t now_ms, uint32_t since_ms, uint32_t after_ms) {
    return (int32_t)(now_ms - since_ms) >= (int32_t)after_ms;
}

// 两个时间里较晚的那个(回绕安全)。
static uint32_t later(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) >= 0 ? a : b;
}

void pupu_power_init(pupu_power_t *p, uint32_t now_ms) {
    *p = (pupu_power_t){
        .stage = PUPU_PWR_ACTIVE,
        .idle_since_ms = now_ms,
        .off_since_ms = now_ms,
        .guard_key = -1,
    };
}

static void guard_expire(pupu_power_t *p, uint32_t now_ms) {
    if (!p->guard) return;
    if ((p->guard_released && reached(now_ms, p->guard_release_ms, PUPU_WAKE_SWALLOW_MS)) ||
        reached(now_ms, p->guard_start_ms, PUPU_WAKE_GUARD_MAX_MS)) {
        p->guard = false;
        p->guard_key = -1;
    }
}

bool pupu_power_key(pupu_power_t *p, pupu_key_t key, pupu_key_ev_t ev, uint32_t now_ms,
                    pupu_power_do_t *todo) {
    *todo = PUPU_PWR_DO_NONE;
    guard_expire(p, now_ms);
    if (p->guard) {
        if (ev == PUPU_KEV_RELEASE && (int8_t)key == p->guard_key && !p->guard_released) {
            p->guard_released = true;
            p->guard_release_ms = now_ms;
        }
        p->idle_since_ms = now_ms;
        return true;
    }

    switch (p->stage) {
    case PUPU_PWR_OFF:
        if (ev != PUPU_KEV_PRESS) return true;  // 亮屏只认 PRESS
        p->stage = PUPU_PWR_ACTIVE;
        p->idle_since_ms = now_ms;
        p->sleep_pending = false;
        p->guard = true;
        p->guard_key = (int8_t)key;
        p->guard_released = false;
        p->guard_start_ms = now_ms;
        *todo = PUPU_PWR_DO_WAKE;
        return true;
    case PUPU_PWR_DIM:
        p->stage = PUPU_PWR_ACTIVE;
        p->idle_since_ms = now_ms;
        *todo = PUPU_PWR_DO_UNDIM;
        return false;
    default:
        p->idle_since_ms = now_ms;
        return false;
    }
}

pupu_power_do_t pupu_power_tick(pupu_power_t *p, uint32_t now_ms, bool busy) {
    guard_expire(p, now_ms);
    if (p->sleep_pending) return PUPU_PWR_DO_NONE;
    switch (p->stage) {
    case PUPU_PWR_ACTIVE:
        if (busy) {
            p->idle_since_ms = now_ms;  // 忙碌结束也算一次活动
            return PUPU_PWR_DO_NONE;
        }
        if (!reached(now_ms, p->idle_since_ms, PUPU_DIM_AFTER_MS)) return PUPU_PWR_DO_NONE;
        p->stage = PUPU_PWR_DIM;
        return PUPU_PWR_DO_DIM;
    case PUPU_PWR_DIM:
        if (busy || !reached(now_ms, p->idle_since_ms, PUPU_OFF_AFTER_MS)) {
            return PUPU_PWR_DO_NONE;
        }
        p->stage = PUPU_PWR_OFF;
        p->off_since_ms = now_ms;
        return PUPU_PWR_DO_OFF;
    default:
        if (busy || !reached(now_ms, later(p->off_since_ms, p->idle_since_ms),
                             PUPU_SLEEP_AFTER_OFF_MS)) {
            return PUPU_PWR_DO_NONE;
        }
        p->sleep_pending = true;
        return PUPU_PWR_DO_SLEEP;
    }
}

pupu_power_do_t pupu_power_screen_off(pupu_power_t *p, uint32_t now_ms) {
    if (p->stage == PUPU_PWR_OFF) return PUPU_PWR_DO_NONE;
    p->stage = PUPU_PWR_OFF;
    p->off_since_ms = now_ms;
    p->idle_since_ms = now_ms;
    return PUPU_PWR_DO_OFF;
}

void pupu_power_sleep_refused(pupu_power_t *p, uint32_t now_ms) {
    p->sleep_pending = false;
    // 让"熄屏已满 5 分钟"在 PUPU_SLEEP_RETRY_MS 以后再次成立
    p->off_since_ms = now_ms - (PUPU_SLEEP_AFTER_OFF_MS - PUPU_SLEEP_RETRY_MS);
    p->idle_since_ms = p->off_since_ms;
}

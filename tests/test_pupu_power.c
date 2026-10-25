// pupu_power 的 host 测试:变暗、熄屏、深睡的计时,忙碌阻挡,亮屏时吞掉多余按键。
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "pupu_config.h"
#include "pupu_power.h"

#define UP PUPU_KEY_UP
#define DOWN PUPU_KEY_DOWN
#define OK PUPU_KEY_OK
#define DIM_MS PUPU_DIM_AFTER_MS
#define OFF_MS PUPU_OFF_AFTER_MS
#define SLEEP_MS PUPU_SLEEP_AFTER_OFF_MS

// 返回 true 表示这个事件交给了页面(没被吞)。
static bool key(pupu_power_t *p, pupu_key_t k, pupu_key_ev_t ev, uint32_t t, pupu_power_do_t *todo) {
    pupu_power_do_t dummy;
    return !pupu_power_key(p, k, ev, t, todo ? todo : &dummy);
}

// 从 t0 起每 50 ms 跑一次主循环,直到 t1(含),返回期间第一个非 NONE 的动作和它的时间。
static pupu_power_do_t run(pupu_power_t *p, uint32_t t0, uint32_t t1, bool busy, uint32_t *at) {
    for (uint32_t t = t0;; t += 50) {
        if ((int32_t)(t - t1) > 0) break;
        const pupu_power_do_t d = pupu_power_tick(p, t, busy);
        if (d != PUPU_PWR_DO_NONE) {
            if (at) *at = t;
            return d;
        }
    }
    return PUPU_PWR_DO_NONE;
}

static void test_stage_timing(uint32_t t0) {
    pupu_power_t p;
    pupu_power_init(&p, t0);
    uint32_t at = 0;
    assert(pupu_power_tick(&p, t0 + DIM_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, t0 + DIM_MS, false) == PUPU_PWR_DO_DIM);
    assert(p.stage == PUPU_PWR_DIM);
    assert(pupu_power_tick(&p, t0 + OFF_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, t0 + OFF_MS, false) == PUPU_PWR_DO_OFF);
    assert(p.stage == PUPU_PWR_OFF);
    assert(run(&p, t0 + OFF_MS + 50, t0 + OFF_MS + SLEEP_MS - 1, false, NULL) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, t0 + OFF_MS + SLEEP_MS, false) == PUPU_PWR_DO_SLEEP);
    // 等深睡的时候不再重复要求
    assert(run(&p, t0 + OFF_MS + SLEEP_MS + 50, t0 + OFF_MS + SLEEP_MS + 60000, false, &at) ==
           PUPU_PWR_DO_NONE);
}

static void test_keys_reset_idle_and_dim_passes_through(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    // ACTIVE 时按键照常交给页面,并重新计时
    assert(key(&p, DOWN, PUPU_KEV_PRESS, 15000, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(key(&p, DOWN, PUPU_KEV_RELEASE, 15080, &todo));
    assert(pupu_power_tick(&p, 15080 + DIM_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, 15080 + DIM_MS, false) == PUPU_PWR_DO_DIM);
    // DIM 时屏幕还看得见:按键交给页面,同时恢复亮度
    assert(key(&p, OK, PUPU_KEV_PRESS, 40000, &todo) && todo == PUPU_PWR_DO_UNDIM);
    assert(p.stage == PUPU_PWR_ACTIVE);
    assert(key(&p, OK, PUPU_KEV_RELEASE, 40080, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(key(&p, OK, PUPU_KEV_CLICK, 40265, &todo) && todo == PUPU_PWR_DO_NONE);
    // 重新从 ACTIVE 开始数 20 秒
    assert(pupu_power_tick(&p, 40265 + DIM_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, 40265 + DIM_MS, false) == PUPU_PWR_DO_DIM);
}

// 熄屏中按一下 OK:亮屏,但 PRESS、RELEASE 和 185 ms 后迟到的 CLICK 都不交给页面。
static void test_wake_swallows_trailing_click(uint32_t t0) {
    pupu_power_t p;
    pupu_power_init(&p, t0);
    pupu_power_do_t todo;
    assert(pupu_power_screen_off(&p, t0) == PUPU_PWR_DO_OFF);
    const uint32_t t = t0 + 60000;
    assert(!key(&p, OK, PUPU_KEV_PRESS, t, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(p.stage == PUPU_PWR_ACTIVE);
    assert(!key(&p, OK, PUPU_KEV_RELEASE, t + 80, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(!key(&p, OK, PUPU_KEV_CLICK, t + 80 + 185, &todo));
    assert(!key(&p, OK, PUPU_KEV_DOUBLE, t + 80 + PUPU_WAKE_SWALLOW_MS - 1, &todo));
    // 满 250 ms 以后正常
    assert(key(&p, DOWN, PUPU_KEV_PRESS, t + 80 + PUPU_WAKE_SWALLOW_MS, &todo));
    assert(todo == PUPU_PWR_DO_NONE && !p.guard);
}

// 按住亮屏的键:长按、别的键(幻影键)都吞掉,直到松开后 250 ms。
static void test_wake_hold_and_phantom(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    pupu_power_screen_off(&p, 0);
    assert(!key(&p, UP, PUPU_KEV_PRESS, 1000, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(!key(&p, UP, PUPU_KEV_LONG, 1500, &todo));
    assert(!key(&p, OK, PUPU_KEV_PRESS, 1700, &todo));    // 幻影键
    assert(!key(&p, OK, PUPU_KEV_RELEASE, 1720, &todo));  // 不是亮屏的键,不开始倒数
    assert(!key(&p, OK, PUPU_KEV_CLICK, 1905, &todo));
    assert(!key(&p, OK, PUPU_KEV_CLICK, 3000, &todo));    // 亮屏的键还按着
    assert(!key(&p, UP, PUPU_KEV_RELEASE, 4000, &todo));
    assert(!key(&p, DOWN, PUPU_KEV_PRESS, 4100, &todo));  // 松开不到 250 ms
    assert(key(&p, DOWN, PUPU_KEV_PRESS, 4250, &todo));
    // 主循环自己也会让吞键结束
    pupu_power_screen_off(&p, 5000);
    assert(!key(&p, OK, PUPU_KEV_PRESS, 6000, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(!key(&p, OK, PUPU_KEV_RELEASE, 6050, &todo));
    pupu_power_tick(&p, 6300, false);
    assert(!p.guard);
}

// 亮屏的键卡住、一直不松开:最多吞 10 秒。
static void test_wake_guard_deadline(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    pupu_power_screen_off(&p, 0);
    assert(!key(&p, OK, PUPU_KEV_PRESS, 1000, &todo));
    assert(!key(&p, OK, PUPU_KEV_LONG, 1000 + PUPU_WAKE_GUARD_MAX_MS - 1, &todo));
    assert(key(&p, UP, PUPU_KEV_PRESS, 1000 + PUPU_WAKE_GUARD_MAX_MS, &todo));
}

// 熄屏中的其他事件不亮屏:比如长按 OK 熄屏后,那个 OK 才松开。
static void test_off_only_press_wakes(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    assert(key(&p, OK, PUPU_KEV_PRESS, 1000, &todo));
    assert(key(&p, OK, PUPU_KEV_LONG, 1500, &todo));
    assert(pupu_power_screen_off(&p, 1500) == PUPU_PWR_DO_OFF);
    assert(pupu_power_screen_off(&p, 1600) == PUPU_PWR_DO_NONE);  // 已经熄了
    assert(!key(&p, OK, PUPU_KEV_RELEASE, 2200, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(!key(&p, OK, PUPU_KEV_CLICK, 2400, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(!key(&p, DOWN, PUPU_KEV_LONG, 2500, &todo) && todo == PUPU_PWR_DO_NONE);
    assert(p.stage == PUPU_PWR_OFF);
    // 长按熄屏:从熄屏那一刻起 5 分钟深睡
    assert(pupu_power_tick(&p, 1500 + SLEEP_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, 1500 + SLEEP_MS, false) == PUPU_PWR_DO_SLEEP);
}

static void test_busy_blocks_and_counts_as_activity(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    uint32_t at = 0;
    // 忙了 2 分钟(比如同步):期间不变暗;忙完以后从那一刻起重新数 20 秒
    assert(run(&p, 0, 120000, true, NULL) == PUPU_PWR_DO_NONE);
    assert(run(&p, 120050, 200000, false, &at) == PUPU_PWR_DO_DIM);
    assert(at == 120000 + DIM_MS);

    // DIM 里忙碌只是先不往下走,不重新计时
    pupu_power_init(&p, 0);
    assert(pupu_power_tick(&p, DIM_MS, false) == PUPU_PWR_DO_DIM);
    assert(pupu_power_tick(&p, OFF_MS, true) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, OFF_MS + 50, false) == PUPU_PWR_DO_OFF);
    assert(p.stage == PUPU_PWR_OFF);

    // OFF 里忙碌(比如存档还在写)挡住深睡;忙完以后满 5 分钟就睡,不从头数
    const uint32_t off = OFF_MS + 50;
    assert(pupu_power_tick(&p, off + SLEEP_MS, true) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, off + SLEEP_MS + 50, false) == PUPU_PWR_DO_SLEEP);
}

static void test_sleep_refused_retries(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_screen_off(&p, 0);
    assert(pupu_power_tick(&p, SLEEP_MS, false) == PUPU_PWR_DO_SLEEP);
    pupu_power_sleep_refused(&p, SLEEP_MS + 300);
    assert(p.stage == PUPU_PWR_OFF && !p.sleep_pending);
    uint32_t at = 0;
    assert(run(&p, SLEEP_MS + 350, SLEEP_MS + 60000, false, &at) == PUPU_PWR_DO_SLEEP);
    assert(at >= SLEEP_MS + 300 + PUPU_SLEEP_RETRY_MS && at < SLEEP_MS + 300 + PUPU_SLEEP_RETRY_MS + 50);
    // 拒绝以后按键照常亮屏
    pupu_power_sleep_refused(&p, at);
    pupu_power_do_t todo;
    assert(!key(&p, DOWN, PUPU_KEV_PRESS, at + 1000, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(p.stage == PUPU_PWR_ACTIVE);
}

// 亮屏、恢复亮度的那一下也重新开始数 20 秒。
static void test_wake_and_undim_restart_idle(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    assert(pupu_power_tick(&p, DIM_MS, false) == PUPU_PWR_DO_DIM);
    assert(key(&p, UP, PUPU_KEV_PRESS, 30000, &todo) && todo == PUPU_PWR_DO_UNDIM);
    assert(pupu_power_tick(&p, 30000 + DIM_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(pupu_power_tick(&p, 30000 + DIM_MS, false) == PUPU_PWR_DO_DIM);

    pupu_power_screen_off(&p, 60000);
    assert(!key(&p, UP, PUPU_KEV_PRESS, 900000, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(!key(&p, UP, PUPU_KEV_RELEASE, 900050, &todo));
    assert(pupu_power_tick(&p, 900000 + DIM_MS - 1, false) == PUPU_PWR_DO_NONE);
    assert(p.stage == PUPU_PWR_ACTIVE);
}

// 按键事件的时间比主循环这一圈的 now 还晚一点(主循环正忙时按键回调又放进来一个):
// 不能当成"已经过了很久",吞键窗口和变暗计时都不能被打乱。
static void test_key_time_after_tick_time(void) {
    pupu_power_t p;
    pupu_power_init(&p, 0);
    pupu_power_do_t todo;
    pupu_power_screen_off(&p, 1000);
    assert(!key(&p, OK, PUPU_KEV_PRESS, 59990, &todo) && todo == PUPU_PWR_DO_WAKE);
    assert(!key(&p, OK, PUPU_KEV_RELEASE, 60030, &todo));
    assert(pupu_power_tick(&p, 60000, false) == PUPU_PWR_DO_NONE);  // 不是 DO_DIM
    assert(p.guard && p.stage == PUPU_PWR_ACTIVE);
    assert(!key(&p, OK, PUPU_KEV_CLICK, 60030 + 185, &todo));        // 迟到的单击照样吞掉
    assert(key(&p, DOWN, PUPU_KEV_PRESS, 60030 + PUPU_WAKE_SWALLOW_MS, &todo));
    // 普通按键也一样:时间稍晚的按键不会让屏幕马上变暗
    assert(key(&p, DOWN, PUPU_KEV_PRESS, 70020, &todo));
    assert(pupu_power_tick(&p, 70000, false) == PUPU_PWR_DO_NONE);
    assert(p.stage == PUPU_PWR_ACTIVE);
}

int main(void) {
    test_stage_timing(0);
    test_stage_timing(UINT32_MAX - 30000);  // 毫秒计数在中途回绕
    test_keys_reset_idle_and_dim_passes_through();
    test_wake_swallows_trailing_click(0);
    test_wake_swallows_trailing_click(UINT32_MAX - 60100);  // 回绕正好落在亮屏和松开之间
    test_wake_hold_and_phantom();
    test_wake_guard_deadline();
    test_off_only_press_wakes();
    test_busy_blocks_and_counts_as_activity();
    test_sleep_refused_retries();
    test_wake_and_undim_restart_idle();
    test_key_time_after_tick_time();
    return 0;
}

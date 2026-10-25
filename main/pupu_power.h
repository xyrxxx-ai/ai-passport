// main/pupu_power.h —— 亮屏 → 变暗 → 熄屏 → 深睡的状态机,以及亮屏时吞掉多余按键(纯逻辑)。
//
//   ACTIVE ──20 秒没动静──▶ DIM(背光 30%)──到 45 秒──▶ OFF(DISPOFF、背光 0)──再过 5 分钟──▶ 深睡
//      ▲                       │                          │
//      └──────── 任意按键 ──────┴──────── 只认 PRESS ─────┘     (从深睡唤醒等于重新开机)
//
// - "动静"指按键事件和"忙碌"(音效在放、噗噗在吃、正在孵蛋、小游戏进行中、存档在写……)。
//   在 ACTIVE 里忙碌期间空闲计时一直重来,所以忙碌结束也算一次活动:同步 2 分钟结束后
//   不会马上熄屏。在 DIM 和 OFF 里忙碌只是先不往下走,不重新计时(比如进 DIM 时存档的
//   那几十毫秒,不应该把熄屏推迟 20 秒)。
// - DIM 时屏幕还看得见:按键照常起作用,同时恢复亮度。
// - OFF 时只有 PRESS 能亮屏,这个 PRESS 不交给页面。从这一刻起,所有键的事件都吞掉,直到
//   亮屏那个键 RELEASE 之后 250 ms:按键驱动在松开约 185 ms 后才发 CLICK,不吞的话会多出
//   一次单击。键还按着时别的键的事件也吞掉,它们多半是 ADC 误读出来的幻影键。
// - OFF 时其他事件(比如长按 OK 熄屏以后,那个 OK 的 RELEASE)直接吞掉,不亮屏。
// - 首页长按 OK 直接进 OFF(pupu_power_screen_off)。
// - 深睡:返回 DO_SLEEP 以后由调用方去做(先同步存档,有键按着就拒绝);拒绝时调用
//   pupu_power_sleep_refused(),10 秒后再试。
//
// 时间都是调用方传进来的真实毫秒(不受时间倍率影响,可以回绕)。本模块不碰硬件。
// 线程:只在 LVGL 任务(app 主循环)里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pupu_input.h"

typedef enum {
    PUPU_PWR_ACTIVE = 0,
    PUPU_PWR_DIM,
    PUPU_PWR_OFF,
} pupu_power_stage_t;

// 调用方要做的事。
typedef enum {
    PUPU_PWR_DO_NONE = 0,
    PUPU_PWR_DO_DIM,    // 背光调到 30%,存一次档
    PUPU_PWR_DO_UNDIM,  // DIM → ACTIVE:背光恢复 100%
    PUPU_PWR_DO_OFF,    // 发 DISPOFF、背光 0、停画面更新,存一次档
    PUPU_PWR_DO_WAKE,   // OFF → ACTIVE:先发 DISPON,再开背光,恢复画面更新
    PUPU_PWR_DO_SLEEP,  // 进入深睡(调用方负责同步存档和检查按键)
} pupu_power_do_t;

typedef struct {
    uint8_t stage;           // pupu_power_stage_t
    uint32_t idle_since_ms;  // 最后一次按键或忙碌的时间
    uint32_t off_since_ms;   // 进入 OFF 的时间(深睡拒绝后会往后挪)
    bool sleep_pending;      // 已经返回过 DO_SLEEP,等深睡或者拒绝

    bool guard;              // 亮屏后正在吞按键
    int8_t guard_key;        // 亮屏的那个键
    bool guard_released;     // 那个键已经松开,正在等 250 ms
    uint32_t guard_start_ms;
    uint32_t guard_release_ms;
} pupu_power_t;

void pupu_power_init(pupu_power_t *p, uint32_t now_ms);

// 每个按键事件先交给这里,再交给 pupu_input。返回 true 表示吞掉(不交给 pupu_input)。
// *todo 是这个事件引起的动作(DO_UNDIM、DO_WAKE 或 DO_NONE)。
bool pupu_power_key(pupu_power_t *p, pupu_key_t key, pupu_key_ev_t ev, uint32_t now_ms,
                    pupu_power_do_t *todo);

// 主循环每次调用一次。busy 见文件开头。返回 DO_DIM、DO_OFF、DO_SLEEP 或 DO_NONE。
pupu_power_do_t pupu_power_tick(pupu_power_t *p, uint32_t now_ms, bool busy);

// 首页长按 OK:马上熄屏。已经是 OFF 时返回 DO_NONE。
pupu_power_do_t pupu_power_screen_off(pupu_power_t *p, uint32_t now_ms);

// 深睡被拒绝了(比如有键按着):PUPU_SLEEP_RETRY_MS 以后再返回一次 DO_SLEEP。
void pupu_power_sleep_refused(pupu_power_t *p, uint32_t now_ms);

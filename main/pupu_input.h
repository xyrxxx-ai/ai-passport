// main/pupu_input.h —— 把按键事件翻译成噗噗的"动作"(纯逻辑,只用标准 C)。
//
// 按键驱动的真实行为(开发计划第 5 节):PRESS 按下 5–10 ms 就到;CLICK/DOUBLE 要在松开约
// 185 ms 后才到;快速按两下只发 DOUBLE;LONG 在按住 500 ms 时发一次,之后不再补发 CLICK。
// 三个键共用一个 ADC 引脚,按住一个键时可能误读成另一个键("幻影键")。所以这里:
//   - ↑/↓ 用 PRESS(按下就响应,不会被合并);OK 用 CLICK 或 DOUBLE 当一次确认,LONG 返回;
//   - 别的键还按着,或者刚松开不到 PUPU_KEY_SWITCH_MS,另一个键的 PRESS 当作幻影键忽略,
//     它后面跟着的 RELEASE、CLICK、DOUBLE、LONG 也一起忽略;
//   - 蛋页按住 OK 计时 3 秒孵化,不靠 LONG(先点一下再按住时驱动不会发 LONG)。
//
// 时间都是调用方传进来的真实毫秒(可以回绕),本模块不碰硬件。线程:只在 LVGL 任务里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 编号和 bsp_button.h 的 bsp_btn_t / bsp_btn_ev_t 一致,设备端用 _Static_assert 确认后再转换。
typedef enum {
    PUPU_KEY_UP = 0,
    PUPU_KEY_DOWN,
    PUPU_KEY_OK,
    PUPU_KEY_COUNT,
} pupu_key_t;

typedef enum {
    PUPU_KEV_PRESS = 0,
    PUPU_KEV_CLICK,
    PUPU_KEV_DOUBLE,
    PUPU_KEV_LONG,
    PUPU_KEV_RELEASE,
} pupu_key_ev_t;

typedef enum {
    PUPU_INPUT_EGG = 0,  // 蛋页:只认"按住 OK"
    PUPU_INPUT_MENU,     // 首页和普通页面
} pupu_input_mode_t;

typedef enum {
    PUPU_ACT_NONE = 0,
    PUPU_ACT_PREV,         // ↑ 按下
    PUPU_ACT_NEXT,         // ↓ 按下
    PUPU_ACT_CONFIRM,      // OK 单击或双击
    PUPU_ACT_BACK,         // OK 长按(首页上以后是熄屏)
    PUPU_ACT_HOLD_START,   // 蛋:开始捂热
    PUPU_ACT_HOLD_CANCEL,  // 蛋:中途松手,取消
    PUPU_ACT_HATCH,        // 蛋:按满 3 秒
} pupu_action_t;

typedef struct {
    int8_t down;                      // 现在按着的键,-1 表示没有
    int8_t last;                      // 最近一次真的被按下的键,-1 表示没有
    uint32_t released_ms;             // 上一次有键松开的时间
    bool any_released;                // released_ms 是否有效
    bool accepted[PUPU_KEY_COUNT];    // 这个键最近一次 PRESS 是真的(不是幻影键)
    bool holding;                     // 蛋:正在捂热
    bool release_pending;             // 蛋:松开了,等抖动宽限期过去
    uint32_t hold_start_ms;
    uint32_t hold_release_ms;
} pupu_input_t;

void pupu_input_init(pupu_input_t *in);

// 处理一个按键事件,返回要执行的动作(大多数事件返回 PUPU_ACT_NONE)。
pupu_action_t pupu_input_key(pupu_input_t *in, pupu_input_mode_t mode, pupu_key_t key,
                             pupu_key_ev_t ev, uint32_t now_ms);

// 主循环每次调用一次:蛋页按满 3 秒返回 HATCH,松手超过宽限期返回 HOLD_CANCEL。
pupu_action_t pupu_input_tick(pupu_input_t *in, uint32_t now_ms);

// 已经捂热了多少毫秒(0 到 PUPU_HOLD_MS),没在捂热时返回 0。用来画进度条。
uint32_t pupu_input_hold_ms(const pupu_input_t *in, uint32_t now_ms);

// 离开蛋页时调用,丢掉正在进行的捂热。
void pupu_input_cancel_hold(pupu_input_t *in);

// main/pupu_save_policy.h —— 存档"读哪个槽、写哪个槽、什么时候写"(纯逻辑,只用标准 C)。
//
// 存档放在 NVS 的两个槽 slot_a、slot_b 里轮流写:写坏一个(比如写到一半断电),
// 另一个还在。开机时选"校验通过、序号更新"的那个槽;下一次写另一个槽,序号加 1。
// 序号用回绕安全的比较((int32_t)(a - b) > 0),写满 2^32 次也不会选错。
//
// 两个特殊情况(开发计划 4.4):
// - 任何一个槽里是更新版本固件写的存档(FUTURE):这次开机一次都不写,免得把它覆盖掉。
//   另一个槽如果能读,就照常读出来玩,只是不保存;
// - 有存档数据,但一个都读不出来:告诉玩家"存档坏了",从新蛋开始。
//
// 存档时机(真实时间,不受时间倍率影响):
// - 状态变了(照顾、孵化、长大……)以后约 1 秒存一次;再有变化就重新等 1 秒;
// - 开机期间每 5 分钟存一次;
// - 电量刚降到 5% 以下时存一次;
// - 屏幕变暗、熄屏时马上存一次(深睡前的那次由 pupu_store_flush 同步写完);
// - "忙碌"时(音效正在放)先不存,忙完再存,避免写 Flash 时音频卡顿爆音。
// 只是换了选中项不算变化,调用方不要为它调用 mark。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pupu_save_codec.h"

// 开机时读到的一个槽。
typedef struct {
    bool present;               // NVS 里有这个键
    pupu_save_status_t status;  // pupu_save_decode() 的结果,只在 present 时有意义
    uint32_t seq;               // 只在 status == PUPU_SAVE_OK 时有意义
} pupu_slot_read_t;

typedef struct {
    int8_t load;        // 读哪个槽:0 = slot_a,1 = slot_b,-1 = 都不能读(从新蛋开始)
    uint8_t write;      // 下一次写哪个槽
    uint32_t next_seq;  // 下一次写的序号
    bool locked;        // 有更新版本固件的存档:这次开机一次都不写
    bool corrupt;       // 有存档数据但都读不出来:要告诉玩家
} pupu_slot_plan_t;

pupu_slot_plan_t pupu_slot_plan(const pupu_slot_read_t slots[2]);

// 每次写成功以后调用:换到另一个槽,序号加 1。
void pupu_slot_advance(pupu_slot_plan_t *p);

typedef struct {
    bool dirty;        // 有变化还没存
    bool battery_low;  // 上一次读到的电量已经低于 5%
    uint32_t due_ms;   // dirty 时,最早什么时候存
    uint32_t last_ms;  // 上一次存的时间
} pupu_autosave_t;

// 时间都是调用方传进来的真实毫秒,可以回绕。
void pupu_autosave_init(pupu_autosave_t *a, uint32_t now_ms);

// 宠物状态变了:约 1 秒后存。
void pupu_autosave_mark(pupu_autosave_t *a, uint32_t now_ms);

// 马上存(变暗、熄屏时):不等 1 秒,但仍然等音效放完。
void pupu_autosave_request(pupu_autosave_t *a, uint32_t now_ms);

// 读到一次电量(百分比,负数表示读不到,忽略)。刚跌到 5% 以下时 mark 一次。
void pupu_autosave_battery(pupu_autosave_t *a, int soc, uint32_t now_ms);

// 现在该不该存。busy = 音效正在放等不适合写 Flash 的时候。
bool pupu_autosave_due(const pupu_autosave_t *a, uint32_t now_ms, bool busy);

// 已经把最新状态交给存档任务了。
void pupu_autosave_done(pupu_autosave_t *a, uint32_t now_ms);

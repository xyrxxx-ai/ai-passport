// main/catch_model.h —— 小游戏「接星星」的规则(纯逻辑,只用标准 C,便于 host 测试)。
//
// 一局:先 3-2-1 倒数(不算时间),然后 25 秒。噗噗在 3 条跑道之间换道,星星和小乌云从右边
// 飞来。物体中心越过接取线的那一帧,按噗噗当时所在的跑道计分,每个物体只算一次:
// 星星 +1;乌云 -1(最低 0)。接到 8 颗以上算赢;奖励由 pupu_model_apply_catch_result() 结算。
//
// 固定步长:每步 20 ms,一局 1250 步。调用方按真实时间累加,每满 20 ms 调一次
// catch_tick();一次最多补 5 步(见 pupu_ui_catch.c)。时间倍率不影响小游戏。
//
// 按键(调用方已经滤掉了幻影键,见 pupu_input.h 的 GAME 模式):
// - ↑/↓ 按下:换道,到头就停;倒数时也能先换;暂停时不动。
// - OK 按下:游戏中立刻暂停。暂停中再按下 OK,并且在长按触发前松开:继续。
// - OK 长按:退出,这一局作废(倒数、游戏中、暂停中都可以)。按住 OK 时先暂停,0.5 秒后退出,
//   中间不会恢复。
//
// 随机数是带种子的 xorshift32,同一个种子、同样的按键必定得到同一局。
// 线程:只在 LVGL 任务(app 主循环)里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "catch_layout.h"

#define CATCH_TICK_MS         20
#define CATCH_ROUND_TICKS     1250  // 25 秒
#define CATCH_COUNTDOWN_TICKS 150   // 3-2-1,3 秒
#define CATCH_MAX_ITEMS       8

// 所有可调的数值都在这里;真机试玩后只改这一处。
typedef struct {
    uint8_t spawn_min_ticks;   // 两次出生之间最少隔几步(45 步 = 900 ms)
    uint8_t spawn_max_ticks;   // 最多隔几步(50 步 = 1000 ms)
    uint8_t cloud_pct;         // 出生的物体里乌云占百分之几
    uint8_t speed_q4;          // 每步向左走多少 q4(42 = 2.625 px,约 131 px/s)
    uint8_t first_spawn_tick;  // 开始后第几步出第一个
} catch_tuning_t;

extern const catch_tuning_t catch_tuning;

typedef enum {
    CATCH_ITEM_NONE = 0,  // 空位
    CATCH_ITEM_STAR,
    CATCH_ITEM_CLOUD,
} catch_item_kind_t;

typedef struct {
    uint8_t kind;    // catch_item_kind_t
    uint8_t lane;    // 0–2
    bool resolved;   // 已经越过接取线、算过分
    int32_t cx_q4;   // 中心横坐标,1/16 像素
} catch_item_t;

typedef enum {
    CATCH_READY = 0,  // 3-2-1 倒数
    CATCH_PLAYING,
    CATCH_PAUSED,
    CATCH_FINISHED,   // 25 秒到了,要结算
    CATCH_ABORTED,    // 长按退出,这一局作废
} catch_state_t;

typedef enum {
    CATCH_IN_UP = 0,
    CATCH_IN_DOWN,
    CATCH_IN_OK_PRESS,
    CATCH_IN_OK_RELEASE,
    CATCH_IN_OK_LONG,
} catch_input_t;

// catch_input() 和 catch_tick() 返回的事件,可能同时有好几个。
#define CATCH_EV_LANE     0x0001u  // 换了跑道
#define CATCH_EV_PAUSED   0x0002u
#define CATCH_EV_RESUMED  0x0004u
#define CATCH_EV_EXIT     0x0008u  // 长按退出,state 变成 ABORTED
#define CATCH_EV_START    0x0010u  // 倒数结束,开始计时
#define CATCH_EV_SPAWN    0x0020u
#define CATCH_EV_STAR     0x0040u  // 接到星星
#define CATCH_EV_CLOUD    0x0080u  // 碰到乌云
#define CATCH_EV_MISS     0x0100u  // 星星飞过去了
#define CATCH_EV_FINISHED 0x0200u  // 25 秒到了,state 变成 FINISHED
#define CATCH_EV_FULL     0x0400u  // 该出生了但 8 个位置都满,这次跳过

typedef struct {
    uint8_t state;            // catch_state_t
    uint8_t lane;             // 噗噗在哪条跑道,0 在最上面
    bool resume_on_release;   // 暂停中又按下了 OK:松开时继续
    uint16_t countdown;       // 倒数还剩几步
    uint16_t ticks;           // 已经玩了几步,0–1250
    uint16_t next_spawn;      // 第几步出下一个
    uint8_t stars;            // 现在有几颗
    uint8_t caught;           // 一共接到几颗(不扣乌云)
    uint8_t clouds_hit;
    uint8_t spawned_stars;
    uint8_t spawned_clouds;
    uint32_t rng;
    catch_item_t items[CATCH_MAX_ITEMS];
} catch_model_t;

typedef struct {
    bool completed;  // 玩满了 25 秒;中途退出是 false
    unsigned stars;
} catch_result_t;

// 新的一局:状态 READY、噗噗在中间跑道。seed 为 0 时换成一个固定的非 0 值。
void catch_init(catch_model_t *m, uint32_t seed);

uint32_t catch_input(catch_model_t *m, catch_input_t in);

// 推进一步(20 ms)。READY 时只倒数;PAUSED、FINISHED、ABORTED 时什么都不做。
uint32_t catch_tick(catch_model_t *m);

// 还剩几秒(向上取整,25 到 0)。倒数时是 25。
unsigned catch_seconds_left(const catch_model_t *m);

// 倒数显示的数字:READY 时是 3、2、1,其他时候是 0。
unsigned catch_countdown_number(const catch_model_t *m);

catch_result_t catch_result(const catch_model_t *m);

// 倒数或者游戏中(不变暗、不熄屏)。暂停不算。
bool catch_active(const catch_model_t *m);

// 一局还没结束(倒数、游戏中、暂停中):这时不定时存档。
bool catch_in_round(const catch_model_t *m);

// 物体从出生到越过接取线要走几步。出生得比这晚的物体来不及计分,就不再出生。
uint16_t catch_travel_ticks(void);

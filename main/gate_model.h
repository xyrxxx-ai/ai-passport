// main/gate_model.h —— 小游戏「羊过门」的规则(纯逻辑,只用标准 C,便于 host 测试)。
//
// 一局:选难度(普通 / 学霸)→ 3-2-1 倒数(不算时间)→ 6 波 → 狼群结算 → 结果牌。
// 每一波三条跑道各有一张静止的算式卡片;倒计时到了,噗噗所在跑道那张卡片滑到它身边生效:
// 羊群 N(起始 10,范围 1–999,结果向下取整并截到范围内)变成卡片算出来的值。
// 每波先按当前 N 定三个结果值 best > second > third(second × 100 ≥ best × 80,
// third × 100 ≤ best × 60,互不相同、都在 1–999 内),再给每个值挑一种写法,所以"数字看起来
// 最大"的卡不一定最好。表现比 R(千分比,起始 1000)每波 R = R × chosen / best(至少 1);
// 6 波后 R ≥ 600 算赢。狼群数量 W = ⌈N × 600 / R⌉ 只用来显示,"N ≥ W"和"R ≥ 600"永远一致。
// 奖励由 pupu_model_apply_gate_result() 结算。规则见 docs/assets/pupu/v2-handoff.zh_CN.md 第 6 节。
//
// 固定步长:每步 20 ms。调用方按真实时间累加,每满 20 ms 调一次 gate_tick();一次最多补 5 步
// (见 pupu_ui_gate.c)。时间倍率不影响小游戏。
//
// 按键(调用方已经滤掉了幻影键,见 pupu_input.h 的 GAME 模式):
// - 选难度时:↑/↓ 切换档位;OK 松开才开始倒数(按住 0.5 秒的长按直接退出,不会先开一局)。
// - ↑/↓ 按下:换道,到头就停;倒数时也能先换;卡片滑动、结算和暂停时不动。
// - OK 按下:波进行中(含卡片滑动)立刻暂停。暂停中再按下 OK,并且在长按触发前松开:继续。
// - OK 长按:退出,这一局作废(选难度、倒数、游戏中、暂停中都可以);狼群结算的 2 秒里不响应。
//
// 随机数是带种子的 xorshift32,同一个种子、同样的按键必定得到同一局。
// 线程:只在 LVGL 任务(app 主循环)里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gate_layout.h"

#define GATE_TICK_MS         20
#define GATE_COUNTDOWN_TICKS 150  // 3-2-1,3 秒
#define GATE_WAVES           6
#define GATE_SHEEP_MAX       999
#define GATE_WOLVES_SHOW_MAX 9999  // 顶栏和结果牌显示 W 时封到这个数
#define GATE_CARD_LINE_MAX   16    // 每行文字的字节数(含结尾 0)

typedef enum { GATE_EASY = 0, GATE_HARD } gate_level_t;

// 所有可调的数值都在这里;真机试玩后只改这一处。
typedef struct {
    uint8_t wave_s[2][GATE_WAVES];  // [档位][第几波] 每波几秒
    uint16_t sheep_start;           // 羊群起始数
    uint16_t win_permille;          // R 至少这么多算赢(千分比)
    uint8_t second_min_pct;         // second ≥ best 的百分之几
    uint8_t third_max_pct;          // third ≤ best 的百分之几
    uint8_t apply_ticks;            // 卡片滑到噗噗身边用几步(15 步 = 0.3 秒)
    uint8_t wolves_ticks;           // 狼群结算几步(100 步 = 2 秒)
    uint8_t mul_waves_max;          // 乘法卡(×2、×3、×1.5、+40%)一局最多出现在几波里
} gate_tuning_t;

extern const gate_tuning_t gate_tuning;

// 写法家族。每波允许哪些家族由档位和第几波决定(见 gate_model.c 的 FAMILIES)。
typedef enum {
    GATE_FAM_ADD = 0,  // +k
    GATE_FAM_SUB,      // −k
    GATE_FAM_MUL,      // ×2、×3
    GATE_FAM_DIV,      // ÷2、÷3
    GATE_FAM_MUL_SUB,  // ×a −b(两步)
    GATE_FAM_ADD_MUL,  // +b ×a(两步)
    GATE_FAM_P40,      // +40%(两步)
    GATE_FAM_M25,      // −25%(两步)
    GATE_FAM_X15,      // ×1.5(两步;学霸第 5–6 波也有)
    GATE_FAM_SQRT,     // +√k
    GATE_FAM_POW,      // +b²、+b³、+b⁴
    GATE_FAM_FACT,     // +n!
    GATE_FAM_INT,      // +∫2x dx / 0→b
    GATE_FAM_DERIV,    // +(x³)′ / x=a
    GATE_FAM_SUM,      // +∑k / k=1→n
    GATE_FAM_COUNT,
} gate_family_t;

typedef struct {
    char line1[GATE_CARD_LINE_MAX];  // UTF-8,最多 9 格(gate_text_cells)
    char line2[GATE_CARD_LINE_MAX];  // 空串表示只有一行
    uint16_t value;                  // 选中它以后 N 变成多少(已截到 1–999)
    uint8_t family;                  // gate_family_t
    uint8_t rank;                    // 0 = best,1 = second,2 = third
    bool mul;                        // 是乘法卡(×2、×3、×1.5、+40%、含 ×a 的两步)
} gate_card_t;

typedef enum {
    GATE_READY_LEVEL = 0,  // 选难度
    GATE_COUNTDOWN,        // 3-2-1
    GATE_WAVE,             // 一波进行中,卡片静止,倒计时条在走
    GATE_APPLYING,         // 选中的卡片滑向噗噗(0.3 秒)
    GATE_PAUSED,
    GATE_WOLVES,           // 第 6 波之后:狼群滑进来,比大小
    GATE_FINISHED,         // 要结算
    GATE_ABORTED,          // 长按退出,这一局作废
} gate_state_t;

typedef enum {
    GATE_IN_UP = 0,
    GATE_IN_DOWN,
    GATE_IN_OK_PRESS,
    GATE_IN_OK_RELEASE,
    GATE_IN_OK_LONG,
} gate_input_t;

// gate_input() 和 gate_tick() 返回的事件,可能同时有好几个。
#define GATE_EV_LANE        0x0001u  // 换了跑道
#define GATE_EV_PAUSED      0x0002u
#define GATE_EV_RESUMED     0x0004u
#define GATE_EV_EXIT        0x0008u  // 长按退出,state 变成 ABORTED
#define GATE_EV_START       0x0010u  // 倒数结束,第一波出现
#define GATE_EV_LEVEL       0x0020u  // 选难度时换了档位
#define GATE_EV_COUNTDOWN   0x0040u  // 松开 OK,开始倒数
#define GATE_EV_WAVE        0x0080u  // 新一波的卡片出现了
#define GATE_EV_APPLYING    0x0100u  // 时间到,选中的卡片开始滑动
#define GATE_EV_PICK_BEST   0x0200u  // 卡片生效:选到了最好的
#define GATE_EV_PICK_SECOND 0x0400u
#define GATE_EV_PICK_THIRD  0x0800u
#define GATE_EV_WOLVES      0x1000u  // 第 6 波生效,狼群开始滑进来
#define GATE_EV_FINISHED    0x2000u  // 结算结束,state 变成 FINISHED

typedef struct {
    uint8_t state;           // gate_state_t
    uint8_t level;           // gate_level_t
    uint8_t lane;            // 噗噗在哪条跑道,0 在最上面
    bool resume_on_release;  // 暂停中又按下了 OK:松开时继续
    uint8_t paused_from;     // 暂停前是 WAVE 还是 APPLYING(gate_state_t)
    uint16_t countdown;      // 倒数还剩几步
    uint8_t wave;            // 第几波,1–6;0 表示还没开始
    uint16_t wave_ticks;     // 这一波已经过了几步
    uint16_t wave_len;       // 这一波一共几步
    uint8_t apply_ticks;     // 卡片滑了几步
    uint8_t chosen_lane;     // 正在滑动的是哪条跑道的卡片
    uint8_t wolves_ticks;    // 结算过了几步
    uint16_t sheep;          // N
    uint16_t r_permille;     // R
    uint8_t mul_waves;       // 已经有几波出过乘法卡
    uint8_t picks[GATE_WAVES];  // 每波选中的 rank(0/1/2),给日志和测试用
    uint8_t stage[GATE_WAVES];  // 每波的卡片是第几步找到的:0 ① 该波家族且 best 增量 ≥ 0.6N,
                                // 1 ② 该波家族,2 ③ 全部家族,3 ④ 退化成加减卡(给日志和测试用)
    uint32_t rng;
    gate_card_t cards[GATE_LANES];  // 当前波的卡片,一条跑道一张
} gate_model_t;

typedef struct {
    bool completed;       // 玩满了 6 波并结算完;中途退出是 false
    bool won;             // completed 且 R ≥ 600
    uint16_t sheep;       // N
    uint32_t wolves;      // W(没封顶)
    uint16_t r_permille;  // R
} gate_result_t;

// 新的一局:状态 READY_LEVEL、噗噗在中间跑道、N = 10、R = 1000。seed 为 0 时换成一个固定的非 0 值。
void gate_init(gate_model_t *m, uint32_t seed, gate_level_t level);

uint32_t gate_input(gate_model_t *m, gate_input_t in);

// 推进一步(20 ms)。READY_LEVEL、PAUSED、FINISHED、ABORTED 时什么都不做。
uint32_t gate_tick(gate_model_t *m);

// 当前波某条跑道的卡片。
const gate_card_t *gate_card(const gate_model_t *m, int lane);

// 这一波还剩多少毫秒(倒计时条用);不在 WAVE 状态时为 0。
unsigned gate_wave_ms_left(const gate_model_t *m);

// 倒数显示的数字:COUNTDOWN 时是 3、2、1,其他时候是 0。
unsigned gate_countdown_number(const gate_model_t *m);

// 狼群数量 W = ⌈N × 600 / R⌉。
uint32_t gate_wolves(const gate_model_t *m);

gate_result_t gate_result(const gate_model_t *m);

// 倒数、波、卡片滑动、狼群结算中(不变暗、不熄屏)。选难度和暂停不算。
bool gate_active(const gate_model_t *m);

// 一局还没结束(选难度、倒数、游戏中、暂停中):按键走 GAME 模式,不定时存档。
bool gate_in_round(const gate_model_t *m);

// 卡片文字占几格:ASCII 1 格(% 2 格),× ÷ √ ∫ ∑ → − 2 格,² ³ ⁴ ′ 1 格,中文 2 格。
int gate_text_cells(const char *utf8);

// R 的更新:R × chosen / best,至少 1(用 uint32_t 中间值)。
uint16_t gate_next_permille(uint16_t r, uint16_t chosen, uint16_t best);

// 按 m->level、m->wave、m->sheep、m->mul_waves 和 m->rng 生成当前波的三张卡片。
// gate_tick() 自己会调;单独公开是为了让测试对 N = 1…999 的每个波位都检查一遍。
void gate_make_wave(gate_model_t *m);

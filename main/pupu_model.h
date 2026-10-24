// main/pupu_model.h —— 噗噗的宠物规则(纯逻辑,只用标准 C,便于 host 测试)。
//
// 整个宠物状态是一个平坦的结构体,所有规则都是对它的函数。规则数值在 pupu_config.h。
//
// 时间只有"宠物时间"一种(真实秒数 × 倍率,换算在 pupu_clock.h)。两个计数器:
// - life:推动数值下降。在线时照常走;离开期间的补算最多走 8 小时。
// - day_s:数"过了几天"。在线时照常走;离开期间按完整的离开时长走,不封顶。
//   每跨过 24 小时,当天的亲密、星星糖和首次同步计数清零。
// 在线推进和补算共用同一个按分钟走的函数,所以两条路径的规则完全一致。
//
// 本模块不知道"现在几点",也不碰硬件。线程:只在 LVGL 任务(app 主循环)里调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PUPU_STAGE_EGG = 0,
    PUPU_STAGE_CHILD,
    PUPU_STAGE_ADULT,
    PUPU_STAGE_COUNT,
} pupu_stage_t;

typedef enum {
    PUPU_FOOD_RICE = 0,  // 饭团
    PUPU_FOOD_COOKIE,    // 小饼干
    PUPU_FOOD_CANDY,     // 星星糖
} pupu_food_t;

typedef enum {
    PUPU_OK = 0,
    PUPU_TIRED,        // 饼干吃腻了:饱腹照加,开心不加
    PUPU_REFUSED,      // 太饱,摇头不吃,什么都没变
    PUPU_NO_CANDY,     // 没有星星糖,什么都没变
    PUPU_DODGED,       // 摸太多次,躲开了,什么都没变
    PUPU_LOST,         // 小游戏完成了但没到 8 颗:只扣饱腹
    PUPU_ABORTED,      // 小游戏中途退出,不算一局:什么都没变
    PUPU_NOT_HATCHED,  // 还是蛋,不能做这件事
} pupu_result_t;

// 一次动作实际造成的变化(已经过上限截断),结果页靠它告诉玩家"得到了什么"。
// 例如小游戏赢了但 candy == 0,就是"今天的星星糖领完啦"(或者库存满 99)。
typedef struct {
    pupu_result_t result;
    int8_t full;
    int8_t happy;
    uint8_t bond;
    int8_t candy;  // 小游戏得到 +1,喂星星糖 -1
    bool grew;     // 这次动作让噗噗长成了成年
} pupu_outcome_t;

typedef struct {
    uint32_t applied_s;  // 实际补算了多少宠物秒(最多 8 小时)
    uint32_t days;       // 按完整离开时长跨过了几个宠物日
} pupu_catch_up_t;

typedef struct {
    uint8_t stage;  // pupu_stage_t
    uint8_t full;   // 饱腹 0–100
    uint8_t happy;  // 开心 0–100
    uint16_t bond;  // 亲密,只增不减
    uint8_t candy;  // 星星糖库存 0–99

    uint32_t life_min;  // 活了多少整分钟(宠物时间);衰减按它的整除关系触发
    uint8_t life_sec;   // 不满一分钟的秒数 0–59,下次推进时进位
    uint32_t day_s;     // 当前宠物日过去了多少秒 0–86399

    uint8_t bond_today;   // 今天已经加了多少亲密 0–20
    uint8_t candy_today;  // 今天从小游戏得了几颗星星糖 0–3
    bool synced_today;    // 今天是否已经领过首次同步奖励

    uint8_t cookie_count;    // 当前窗口里吃了几块饼干
    uint8_t cookie_win_min;  // 饼干窗口还剩几分钟,0 表示没开
    uint8_t pat_count;       // 当前窗口里有效的摸摸次数
    uint8_t pat_win_min;     // 摸摸窗口还剩几分钟,0 表示没开

    bool has_fed;           // 喂过东西没有
    uint32_t fed_life_min;  // 上次喂食时的 life_min
    uint32_t fed_acct;      // 上次喂食时的 acct_now(真实 Unix 秒),0 表示当时没同步过
} pupu_model_t;

// 新蛋:饱腹 80、开心 80,其余为 0。
void pupu_model_init(pupu_model_t *m);

// 蛋 → 幼年。返回 true 表示这次孵化了;已经孵化过就什么都不做、返回 false。
bool pupu_model_hatch(pupu_model_t *m);

// 在线推进 pet_s 宠物秒。状态可以降到 0。蛋阶段只走时间、不衰减。
void pupu_model_advance(pupu_model_t *m, uint32_t pet_s);

// 离开期间的补算。数值最多只按 8 小时推进,并且每个数的下限是
// min(10, 补算开始时的值):不会压到 10 以下,本来低于 10 的也不会被抬高。
// day_s 按完整的 away_pet_s 前进。away_pet_s 是已经乘过倍率的宠物秒。
pupu_catch_up_t pupu_model_catch_up(pupu_model_t *m, uint64_t away_pet_s);

// 喂食。acct_now 是当前的纪元记账秒(见 pupu_clock.h),没同步过就传 0。
pupu_outcome_t pupu_model_feed(pupu_model_t *m, pupu_food_t food, uint32_t acct_now);

// 摸摸头。
pupu_outcome_t pupu_model_pat(pupu_model_t *m);

// 小游戏结算,第一次收到"结束"事件时只调用一次。completed=false 表示中途退出。
pupu_outcome_t pupu_model_apply_catch_result(pupu_model_t *m, bool completed, unsigned stars);

// 收到一条被接受的对时之后调用(调用方要先做完补算)。
// 每个宠物日第一次调用时亲密 +2,同一天再调用不加。
pupu_outcome_t pupu_model_apply_sync(pupu_model_t *m);

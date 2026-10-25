// main/pupu_model.h —— 噗噗的宠物规则(纯逻辑,只用标准 C,便于 host 测试)。
//
// 整个宠物状态是一个平坦的结构体,所有规则都是对它的函数。规则数值在 pupu_config.h。
//
// 时间只有"宠物时间"一种(真实秒数 × 倍率,换算在 pupu_clock.h)。两个计数器:
// - life:推动数值下降。在线时照常走;离开期间的补算最多走 8 小时。
// - day_s:数"过了几天"。在线时照常走;离开期间按完整的离开时长走,不封顶。
//   每跨过 24 小时,当天的亲密、星星糖、打工次数和首次同步计数清零。
// 在线推进和补算共用同一个按分钟走的函数,所以两条路径的规则完全一致。
//
// v2 新增(规则见 docs/assets/pupu/v2-handoff.zh_CN.md 第 1–5 节):
// - 零花钱 coin:只有打工给,饭团和饼干要花钱,星星糖免费。新孵化的噗噗有 30 元。
// - 摸摸按 10 分钟窗口里的第几次分六档:1–3 次加开心和亲密,第 4 次不耐烦,第 5 次反抗
//   扣开心,第 6 次起只转身(不计数)。
// - 打工按宠物日计数:第 1、2 次赚钱扣开心、占掉当天亲密额度,第 3 次进入 emo,
//   第 4、5 次收益减半,第 6 次起拒绝。
// - emo:emo_min 从 240 每宠物分钟倒数到 0。期间摸摸只躲开、饼干拒绝、饭团照吃、
//   小游戏照常;只有喂星星糖能立刻解除。
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
    PUPU_TIRED,        // 饼干吃腻了:饱腹照加,开心不加,钱照扣
    PUPU_REFUSED,      // 太饱,摇头不吃,什么都没变(不扣钱)
    PUPU_NO_CANDY,     // 没有星星糖,什么都没变
    PUPU_DODGED,       // 窗口内第 6 次起的摸摸:转身,什么都没变、不计数
    PUPU_LOST,         // 小游戏完成了但没赢:只扣饱腹
    PUPU_ABORTED,      // 小游戏中途退出,不算一局:什么都没变
    PUPU_NOT_HATCHED,  // 还是蛋,不能做这件事
    PUPU_NO_COIN,      // 钱不够买饭团或饼干,什么都没变
    PUPU_ANNOYED,      // 窗口内第 4 次摸摸:不耐烦,什么都没加(计了数)
    PUPU_ANGRY,        // 窗口内第 5 次摸摸:激烈反抗,开心 -5
    PUPU_SULKING,      // emo 中摸摸或喂饼干:不想理你 / 没胃口,什么都没变、不计数
    PUPU_WENT_EMO,     // 当天第 3 次打工:照常给钱扣开心,并进入 emo
    PUPU_OVERWORKED,   // 当天第 4、5 次打工:做错表了,零花钱 +3、开心 -10
    PUPU_EXHAUSTED,    // 当天第 6 次起打工:做不动了,什么都没变
} pupu_result_t;

// 一次动作实际造成的变化(已经过上限截断),结果页靠它告诉玩家"得到了什么"。
// 例如小游戏赢了但 candy == 0,就是"今天的星星糖领完啦"(或者库存满 99)。
typedef struct {
    pupu_result_t result;
    int8_t full;
    int8_t happy;
    uint8_t bond;
    int8_t candy;        // 小游戏得到 +1,喂星星糖 -1
    int8_t coin;         // 打工 +10 / +3(封顶时更少),饭团 -5,饼干 -3
    int8_t bond_budget;  // 打工占掉的当天亲密额度(-3,额度快满时更少,已满时 0),给气泡用
    bool emo_cleared;    // 喂星星糖解除了 emo
    bool grew;           // 这次动作让噗噗长成了成年
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

    uint8_t bond_today;   // 今天已经用掉多少亲密额度 0–20(打工占掉的也算在里面)
    uint8_t candy_today;  // 今天从小游戏得了几颗星星糖 0–3
    bool synced_today;    // 今天是否已经领过首次同步奖励

    uint8_t cookie_count;    // 当前窗口里吃了几块饼干
    uint8_t cookie_win_min;  // 饼干窗口还剩几分钟,0 表示没开
    uint8_t pat_count;       // 当前窗口里摸了几次,计到 5 饱和
    uint8_t pat_win_min;     // 摸摸窗口还剩几分钟,0 表示没开

    bool has_fed;           // 喂过东西没有
    uint32_t fed_life_min;  // 上次喂食时的 life_min
    uint32_t fed_acct;      // 上次喂食时的 acct_now(真实 Unix 秒),0 表示当时没同步过

    // v2 新字段,放在末尾(存档 v2 也接在 v1 字段后面,见 pupu_save_codec.h)
    uint8_t coin;        // 零花钱 0–99
    uint8_t work_today;  // 今天打了几次工 0–5,跨宠物日清零
    uint16_t emo_min;    // emo 还剩几宠物分钟,0 表示没在 emo
} pupu_model_t;

// 新蛋:饱腹 80、开心 80、零花钱 30,其余为 0。
void pupu_model_init(pupu_model_t *m);

// 蛋 → 幼年。返回 true 表示这次孵化了;已经孵化过就什么都不做、返回 false。
bool pupu_model_hatch(pupu_model_t *m);

// 在线推进 pet_s 宠物秒。状态可以降到 0。蛋阶段只走时间、不衰减。
void pupu_model_advance(pupu_model_t *m, uint32_t pet_s);

// 离开期间的补算。数值最多只按 8 小时推进,并且每个数的下限是
// min(10, 补算开始时的值):不会压到 10 以下,本来低于 10 的也不会被抬高。
// day_s 按完整的 away_pet_s 前进。away_pet_s 是已经乘过倍率的宠物秒。
// emo 倒计时也最多走 8 小时,所以离开超过 4 小时回来必然已经解除。
pupu_catch_up_t pupu_model_catch_up(pupu_model_t *m, uint64_t away_pet_s);

// 喂食。acct_now 是当前的纪元记账秒(见 pupu_clock.h),没同步过就传 0。
// 判断顺序:蛋 → 没星星糖 → emo 中的饼干(SULKING)→ 太饱(emo 中的星星糖除外)
// → 钱不够 → 生效并扣钱。喂星星糖会解除 emo。
pupu_outcome_t pupu_model_feed(pupu_model_t *m, pupu_food_t food, uint32_t acct_now);

// 摸摸。emo 中返回 PUPU_SULKING(不计数、不开窗口);否则按窗口内第几次分档。
pupu_outcome_t pupu_model_pat(pupu_model_t *m);

// 打工。当天第 1、2 次 PUPU_OK,第 3 次 PUPU_WENT_EMO(emo_min 设成 240,已在 emo 中也重置),
// 第 4、5 次 PUPU_OVERWORKED,第 6 次起 PUPU_EXHAUSTED(什么都不变)。
// 第几次只看 work_today,不看 emo_min。
pupu_outcome_t pupu_model_work(pupu_model_t *m);

// 小游戏「羊过门」结算,第一次收到"结束"事件时只调用一次。completed=false 表示中途退出,
// 什么都不变;完成一局饱腹 -5;won=true 再加开心、亲密和星星糖(每天最多 3 颗)。
// 赢了也不解除 emo。
pupu_outcome_t pupu_model_apply_gate_result(pupu_model_t *m, bool completed, bool won);

// 收到一条被接受的对时之后调用(调用方要先做完补算)。
// 每个宠物日第一次调用时亲密 +2,同一天再调用不加。
pupu_outcome_t pupu_model_apply_sync(pupu_model_t *m);

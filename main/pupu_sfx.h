// main/pupu_sfx.h —— 噗噗的音效:一个小合成器任务,16 kHz 单声道,方波、三角波和噪声。
//
// 写法照搬 demo/tamagezi 的合成器(只借鉴写法,没有合并那个分支),去掉了按物种变调的表。
// 调用方只把"放哪个音效"放进队列就马上返回,合成和写 I2S 都在 pupu_sfx 任务里做。
// 任务优先级 5,比 LVGL 任务(4)高:它大部分时间阻塞在 bsp_audio_write 里等 DMA 有空位,
// 醒来只算 256 个采样,所以不会拖慢画面,画面也不会把音频饿出断音。
//
// 写 Flash 时缓存会暂停,可能让 I2S 中断来不及处理而爆音。所以:
// - sdkconfig.defaults 打开了 CONFIG_I2S_ISR_IRAM_SAFE;
// - 存档等 pupu_sfx_busy() 变成 false 再写(见 pupu_save_policy.h)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    PUPU_SFX_EAT = 0,  // 咀嚼
    PUPU_SFX_HAPPY,    // 喷一下鼻子,开心
    PUPU_SFX_NO,       // 摇头、躲开、没有星星糖
    PUPU_SFX_HATCH,    // 孵化
    PUPU_SFX_GROW,     // 长大
    PUPU_SFX_BEEP,     // 小游戏倒数 3-2-1
    PUPU_SFX_WIN,      // 小游戏:赢了
    PUPU_SFX_LOSE,     // 小游戏:输了
    PUPU_SFX_SYNC,     // 同步好了:挥手时清脆的"叮——"
    PUPU_SFX_PAT,      // 摸摸第 1–3 次:小手拍在头上,"啪"
    PUPU_SFX_ANNOYED,  // 摸摸第 4 次:不耐烦,"哼"
    PUPU_SFX_ANGRY,    // 摸摸第 5 次:激烈反抗,三连降
    PUPU_SFX_COIN,     // 打工收钱,"叮叮"
    PUPU_SFX_THUD,     // 表格砸中头,"砰":低而长的闷响
    PUPU_SFX_SIGH,     // 叹气:emo 中摸摸("不想理你")或喂饼干("没胃口")
    PUPU_SFX_CARD_GOOD,  // 羊过门:选到最佳的卡片
    PUPU_SFX_CARD_BAD,   // 羊过门:选到最差的卡片
    PUPU_SFX_HOWL,       // 羊过门:狼群来了
    PUPU_SFX_COUNT,
} pupu_sfx_t;

// 初始化音频并启动合成任务。失败时游戏照常玩,只是没有声音。只调用一次。
esp_err_t pupu_sfx_start(void);

// 放一个音效。不阻塞,哪个任务都能调用;队列满了就丢掉这一个。
void pupu_sfx_play(pupu_sfx_t sfx);

// 有音效在排队或正在放,或者最后一段声音还在 DMA 里没放完。
bool pupu_sfx_busy(void);

// 停止握手:丢掉排队的音效,切断正在放的,等任务确认以后不会再写 I2S。
// 返回 true 表示已经安全,可以调用 bsp_audio_sleep() 等;之后 pupu_sfx_play() 都被忽略。
// 给深度休眠前用(M4)。
bool pupu_sfx_stop(uint32_t timeout_ms);

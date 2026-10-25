// main/pupu_save_codec.h —— 噗噗存档的逐字段编码和解码(纯逻辑,只用标准 C)。
//
// 不直接存 C 结构体:结构体的填充字节和字段顺序一改,旧存档就全废了。这里每个字段
// 都按固定宽度、小端写出,外面包一层固定不变的记录头和 CRC32:
//
//   magic "PUPU"(u32) | schema_ver(u16) | seq(u32) | len(u16) | payload[len] | crc32(u32)
//
// CRC32 覆盖前面所有字节,算法是 IEEE 标准(多项式 0xEDB88320,初值和末值异或
// 0xFFFFFFFF),和 Python zlib.crc32、手机端 JS 一致。记录头和 CRC 的格式以后
// 永远不改,只有 payload 随 schema_ver 变化。
//
// 版本历史:
// - v1(51 字节 payload):宠物状态 + 对时信息,见 decode_v1()。
// - v2(55 字节 payload):v1 的 51 字节后面接 coin u8、work_today u8、emo_min u16。
//   读到 v1 存档时补成 coin = 30、work_today = 0、emo_min = 0,玩家的噗噗不会被清掉。
//
// 改存档格式时:PUPU_SAVE_VERSION 加 1,在 decode_payload() 里给旧版本加一个
// case(读旧字段,新字段填默认值,这就是迁移函数),并在测试里保留旧版本的
// 逐字节样例,证明旧存档还能读。每个版本的 payload 长度都要单独写成常量:
// 只改一个常量会让所有旧存档被判成 BAD_LENGTH。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pupu_model.h"

#define PUPU_SAVE_VERSION          2
#define PUPU_SAVE_HEADER_SIZE      12
#define PUPU_SAVE_PAYLOAD_SIZE_V1  51
#define PUPU_SAVE_PAYLOAD_SIZE_V2  55
#define PUPU_SAVE_PAYLOAD_SIZE     PUPU_SAVE_PAYLOAD_SIZE_V2  // 当前版本的 payload 长度
#define PUPU_SAVE_RECORD_SIZE      (PUPU_SAVE_HEADER_SIZE + PUPU_SAVE_PAYLOAD_SIZE + 4)  // 71

// 一份存档 = 宠物状态 + 开机补算要用的对时信息。
typedef struct {
    pupu_model_t model;
    uint32_t acct_epoch;  // 存档时的 acct_now(见 pupu_clock.h)
    uint64_t rtc_us;      // 存档时的 esp_rtc_get_time_us()
    uint64_t token;       // 会话令牌
} pupu_save_t;

typedef enum {
    PUPU_SAVE_OK = 0,
    PUPU_SAVE_BAD_LENGTH,   // 太短,或者 len 字段和实际长度对不上,或者 len 不是这个版本的长度
    PUPU_SAVE_BAD_MAGIC,    // 不是噗噗的存档
    PUPU_SAVE_BAD_CRC,      // 数据损坏
    PUPU_SAVE_BAD_VERSION,  // 版本号是 0 或者不认识的旧版本
    PUPU_SAVE_FUTURE,       // 来自更新版本的固件:调用方要提示,并且不能覆盖它
} pupu_save_status_t;

uint32_t pupu_crc32(const uint8_t *data, size_t len);

// 把 s 编码成一条完整的当前版本记录写进 buf。返回写了多少字节;cap 不够时返回 0。
size_t pupu_save_encode(const pupu_save_t *s, uint32_t seq, uint8_t *buf, size_t cap);

// 解码一条记录(v1 或 v2)。只有返回 PUPU_SAVE_OK 时 *out 和 *seq 才有效;旧版本会在这里迁移。
pupu_save_status_t pupu_save_decode(const uint8_t *buf, size_t len, pupu_save_t *out, uint32_t *seq);

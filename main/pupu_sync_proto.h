// main/pupu_sync_proto.h —— 蓝牙同步协议:消息编解码、请求处理、存档分块、同步窗口(纯逻辑)。
//
// 协议见开发计划第 8 节(Mac 网页 tools/pupu-sync-web/ 和以后的小程序共用):
// - 一次 ATT 写入或通知就是一条完整的消息,最长 20 字节:[type u8][seq u8][payload],小端。
// - 每种请求的长度固定,长度不对就回 ERR(能发现 iPhone 悄悄截断的写入)。
// - 设备回复时原样带回请求的 seq。每个请求都有且只有一条回复。
// - 重试由手机负责(等 1 秒没回复就用同一个 seq 重发)。设备的处理是幂等的:
//   同一个 seq、同一个时间的 TIME 只算一次;同一个或更早的时间不会重复补算;
//   同一个 seq 的 SAVE_BEGIN 返回同一份快照,SAVE_GET 总是从这份快照里取。
// - 每个窗口只有第一条被接受的 TIME 会补算、领首次同步奖励。同一个窗口里后面的 TIME
//   只校正 acct_now:v1 不配对,窗口里谁都能连上来,不能让人连发"一天以后"的时间刷奖励。
//   要再补算一次,得在设备上重新打开同步窗口。
//
//   type  方向        内容                                         总长  回复
//   0x01  手机→设备   epoch_s u32、tz_min i16(v1 不用)            8     STATUS(先补算;时间早于
//                                                                         2026 或晚于 2100 回 ERR 3)
//   0x02  手机→设备   —                                            2     STATUS
//   0x03  手机→设备   —                                            2     SAVE_INFO(拍快照)
//   0x04  手机→设备   idx u8                                       3     SAVE_CHUNK
//   0x05  手机→设备   —                                            2     DONE_ACK
//   0x81  STATUS      proto_ver、stage、full、happy u8,bond u16,candy u8,
//                     fed_age_min u16、away_min u16、flags u8      14
//   0x83  SAVE_INFO   save_ver u8、total_len u16、chunk_count u8、crc32 u32   10
//                     (存档是一整条记录,见 pupu_save_codec.h;crc32 覆盖除最后 4 字节
//                     以外的全部字节,也就是等于记录末尾自带的 CRC。不能连末尾的 CRC
//                     一起算:那样不管存档是什么,结果永远是常数 0x2144DF1C)
//   0x84  SAVE_CHUNK  idx u8、data 1–17 字节                       ≤20
//   0x85  DONE_ACK    —                                            2
//   0xEE  ERR         req_type u8、code u8                         4
//
// 逐字节的"标准答案"在 tests/pupu_sync_vectors.txt,C 测试和网页的 JS 测试共用。
//
// 本模块不碰硬件,也不知道蓝牙:调用方把收到的字节交给 pupu_sync_handle(),把回复发出去。
// 线程:只在 LVGL 任务(app 主循环)里调用。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pupu_clock.h"
#include "pupu_model.h"
#include "pupu_save_codec.h"

#define PUPU_SYNC_PROTO_VER 1
#define PUPU_SYNC_MSG_MAX   20  // 一条消息最多 20 字节(默认 MTU 23 减去 3 字节 ATT 头)
#define PUPU_SYNC_CHUNK_MAX 17  // SAVE_CHUNK 每块最多 17 字节存档数据
#define PUPU_SYNC_AGE_MAX   0xFFFEu  // fed_age_min、away_min 的上限(约 45 天)
#define PUPU_SYNC_NEVER_FED 0xFFFFu  // fed_age_min:还没喂过

typedef enum {
    PUPU_MSG_TIME = 0x01,
    PUPU_MSG_GET_STATUS = 0x02,
    PUPU_MSG_SAVE_BEGIN = 0x03,
    PUPU_MSG_SAVE_GET = 0x04,
    PUPU_MSG_DONE = 0x05,
    PUPU_MSG_STATUS = 0x81,
    PUPU_MSG_SAVE_INFO = 0x83,
    PUPU_MSG_SAVE_CHUNK = 0x84,
    PUPU_MSG_DONE_ACK = 0x85,
    PUPU_MSG_ERR = 0xEE,
} pupu_msg_type_t;

typedef enum {
    PUPU_SYNC_ERR_LENGTH = 1,       // 长度不对
    PUPU_SYNC_ERR_UNKNOWN = 2,      // 不认识的 type
    PUPU_SYNC_ERR_TIME = 3,         // 时间不合理(早于 2026-01-01 或晚于 2100-01-01)
    PUPU_SYNC_ERR_INDEX = 4,        // SAVE_GET 的序号越界
    PUPU_SYNC_ERR_NO_SNAPSHOT = 5,  // 还没有 SAVE_BEGIN 就 SAVE_GET
} pupu_sync_err_t;

// 同步状态。开机时 pupu_sync_init() 一次;每次开窗口 pupu_sync_begin_window()。
typedef struct {
    uint8_t snap[PUPU_SAVE_RECORD_SIZE];  // SAVE_BEGIN 时拍下的存档(一整条记录)
    uint8_t snap_len;                     // 0 表示这个窗口里还没有快照
    uint8_t snap_seq;                     // 拍快照的那条 SAVE_BEGIN 的 seq
    uint32_t snap_crc;                    // 快照除最后 4 字节以外的 CRC32(见上面 SAVE_INFO)
    bool has_time;                        // 这个窗口里收到过被接受的 TIME(补算过了)
    uint8_t time_seq;                     // 最近一条被接受的 TIME 的 seq 和时间
    uint32_t time_epoch;
    uint16_t away_min;  // 本次开机以来最近一次补算(至少 1 分钟)的离开分钟(宠物时间,不封顶);
                        // 0 = 没补算过。对时补算不到 1 分钟真实时间(两边时钟差几秒)时不覆盖它
} pupu_sync_t;

// 处理请求要用到的东西,由调用方准备。
typedef struct {
    pupu_model_t *model;
    pupu_clock_t *clock;
    // SAVE_BEGIN 时调用:填好当前的存档(宠物状态 + 对时信息),和写进 NVS 的一样。
    void (*snapshot)(pupu_save_t *out);
} pupu_sync_env_t;

typedef struct {
    uint8_t reply[PUPU_SYNC_MSG_MAX];
    uint8_t reply_len;     // 回复的长度(每个请求都有回复)
    bool time_accepted;    // 收到一条新的、被接受的 TIME:宠物状态或 acct_now 变了,要存档
    uint64_t away_pet_s;   // 这次 TIME 补算的原始离开时长(宠物秒);超过 1 小时显示"你回来啦"
    pupu_outcome_t bonus;  // 这次 TIME 的首次同步奖励(bond 为 0 表示今天已经领过)
    bool done;             // 收到 DONE:挥手、"叮"一声,然后关窗口
} pupu_sync_result_t;

void pupu_sync_init(pupu_sync_t *s);

// 每次打开同步窗口时调用:丢掉上一个窗口的快照和 TIME 记录。away_min 保留。
void pupu_sync_begin_window(pupu_sync_t *s);

// 窗口里每次有手机连上时调用:丢掉上一次连接的快照(TIME 记录保留,一个窗口只补算一次)。
void pupu_sync_new_link(pupu_sync_t *s);

// 开机按 RTC 补算以后调用。不到 1 分钟时什么都不改。
void pupu_sync_note_away(pupu_sync_t *s, uint64_t away_pet_s);

// 处理一条手机发来的消息。msg 里有 len 个有效字节;len 可以超过 20(那就回 ERR)。
pupu_sync_result_t pupu_sync_handle(pupu_sync_t *s, const pupu_sync_env_t *env,
                                    const uint8_t *msg, size_t len);

// SAVE_CHUNK 的块数:每块 17 字节,最后一块可以更短。
unsigned pupu_sync_chunk_count(size_t total_len);

// 上次喂食过了多少分钟:同步过(acct_now 和喂食时记下的 acct 都不为 0)用真实分钟,
// 否则用宠物分钟。还没喂过返回 0xFFFF,最大 0xFFFE。
uint16_t pupu_sync_fed_age_min(const pupu_model_t *m, uint32_t acct_now);

// ---- 同步窗口(产品说明第 12 节第 7 条)----
// 60 秒内等手机来连;连上过以后,等到 DONE,或者 20 秒没有新请求就结束;整次最长 2 分钟。
// 时间都是调用方传进来的真实毫秒(只在主循环里取,单调,可以回绕)。

typedef enum {
    PUPU_WIN_OPEN = 0,        // 还开着
    PUPU_WIN_CLOSE_DONE,      // 收到 DONE,挥完手了
    PUPU_WIN_CLOSE_NO_PHONE,  // 60 秒没有手机来连
    PUPU_WIN_CLOSE_IDLE,      // 连上过,但 20 秒没有新请求(手机走开了)
    PUPU_WIN_CLOSE_MAX,       // 到了 2 分钟
} pupu_win_close_t;

typedef struct {
    uint32_t open_ms;
    uint32_t activity_ms;  // 最近一次连上或收到请求的时间
    uint32_t done_ms;
    bool ever_connected;
    bool connected;
    bool done;
} pupu_sync_window_t;

void pupu_sync_window_open(pupu_sync_window_t *w, uint32_t now_ms);
// 连上(connected=true)或断开。连上算一次活动。
void pupu_sync_window_link(pupu_sync_window_t *w, bool connected, uint32_t now_ms);
// 收到一条请求(不管格式对不对)。
void pupu_sync_window_request(pupu_sync_window_t *w, uint32_t now_ms);
// 收到 DONE。重复调用不会推迟关窗口。
void pupu_sync_window_done(pupu_sync_window_t *w, uint32_t now_ms);
// 现在该不该关;返回 PUPU_WIN_OPEN 表示还开着。
pupu_win_close_t pupu_sync_window_check(const pupu_sync_window_t *w, uint32_t now_ms);
// 等手机来连的剩余秒数(向上取整);连上过以后返回 0。
unsigned pupu_sync_window_wait_left_s(const pupu_sync_window_t *w, uint32_t now_ms);

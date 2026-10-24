// main/pupu_ui.h —— 噗噗各个页面共用的颜色、布局和页面接口。
//
// 全部只在 LVGL 任务里调用(app 主循环是一个 lv_timer)。同一时间只保留一个页面:
// 进页面时 create 新屏幕,旧屏幕由 pupu_app 删除。界面不用透明度、渐变、变换和切屏动画,
// 这些都会临时从 LVGL 内存池借很多内存。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "pupu_assets.h"
#include "pupu_input.h"
#include "pupu_model.h"
#include "pupu_sync_proto.h"

#define PUPU_COLOR_BG     0xF4ECDD  // 暖色纸面
#define PUPU_COLOR_PAPER  0xFFFDF7  // 气泡
#define PUPU_COLOR_INK    0x2B2118  // 文字和描边
#define PUPU_COLOR_MUTED  0x8C7F6A  // 没选中的文字
#define PUPU_COLOR_SOFT   0xD8CDB6  // 进度条底色、没选中的圆点
#define PUPU_COLOR_ACCENT 0x2F4A8A  // 护照墨水蓝

// 宠物画布 144×144,水平居中
#define PUPU_PET_X 48
#define PUPU_PET_Y 72

#define PUPU_NOTICE_MS 4000  // 开机提示(存档坏了等)显示多久

// 页面处理完一个动作或一次更新以后,请 app 做什么。
typedef enum {
    PUPU_UI_NONE = 0,
    PUPU_UI_CHANGED,  // 宠物状态变了:约 1 秒后存档
    PUPU_UI_GO_HOME,
    PUPU_UI_GO_FEED,
    PUPU_UI_GO_PLAY,  // 也用于从小游戏回到玩耍页
    PUPU_UI_GO_STATUS,
    PUPU_UI_GO_CATCH,    // 玩耍页选"接星星"
    PUPU_UI_GO_SYNC,     // 首页选"同步"
    PUPU_UI_SCREEN_OFF,  // 首页长按 OK:熄屏
} pupu_ui_req_t;

// 宠物现在在做什么;pupu_ui_pet_anim() 按阶段换成对应的动画。
typedef enum {
    PUPU_POSE_IDLE = 0,  // 待机;饱腹低于 15 时自动变成趴着
    PUPU_POSE_EAT,
    PUPU_POSE_HAPPY,
    PUPU_POSE_DODGE,
    PUPU_POSE_SHAKE,
    PUPU_POSE_WAVE,  // 同步好了:挥手
} pupu_pose_t;

pupu_anim_t pupu_ui_pet_anim(const pupu_model_t *m, pupu_pose_t pose);

lv_obj_t *pupu_ui_screen_create(void);
lv_obj_t *pupu_ui_label(lv_obj_t *parent, const char *text, uint32_t color);
lv_obj_t *pupu_ui_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t color, int32_t radius);

// 宠物上方的气泡,最多一个。text 为 NULL 时藏起来;和正在显示的是同一个指针时什么都不做,
// 所以要显示临时拼出来的文字时,先改缓冲区再传一个新的指针(或者先传 NULL)。
typedef struct {
    lv_obj_t *box;
    lv_obj_t *text;
    const char *shown;
} pupu_bubble_t;

void pupu_bubble_create(pupu_bubble_t *b, lv_obj_t *scr);
void pupu_bubble_show(pupu_bubble_t *b, const char *text);

// 底部选择条:中间深色胶囊是选中项,两边淡色字是上一项和下一项,到头就停(不循环)。
// 每一项最多 3 个字。dots=true 时下面再画一排小圆点(首页用;最多 4 项)。
#define PUPU_PICKER_MAX 4
typedef struct {
    const char *const *items;
    int count;
    int selected;
    lv_obj_t *left;
    lv_obj_t *pill;
    lv_obj_t *mid;
    lv_obj_t *right;
    lv_obj_t *dots[PUPU_PICKER_MAX];
    bool has_dots;
} pupu_picker_t;

void pupu_picker_create(pupu_picker_t *p, lv_obj_t *scr, const char *const *items, int count,
                        int selected, bool dots);
// PREV / NEXT 时移动选中项;返回 true 表示真的动了。
bool pupu_picker_action(pupu_picker_t *p, pupu_action_t act);

// 子页面的顶栏标题(左上)和底部"长按 OK 返回"提示。
void pupu_ui_page_frame(lv_obj_t *scr, const char *title);

// ---- 蛋页:按住 OK 3 秒孵化。notice 不为 NULL 时先显示它几秒(比如"存档坏了")----
lv_obj_t *pupu_ui_egg_create(const char *notice, uint32_t now_ms);
pupu_ui_req_t pupu_ui_egg_action(pupu_action_t act, pupu_model_t *m, uint32_t now_ms);
// 孵化动画放完后返回 PUPU_UI_GO_HOME。
pupu_ui_req_t pupu_ui_egg_update(const pupu_input_t *in, uint32_t now_ms);
// 正在放孵化动画(这时不变暗、不熄屏)。
bool pupu_ui_egg_busy(void);

// ---- 首页:顶栏电量、需求气泡、宠物、底部 4 个功能;长按 OK 熄屏 ----
// battery 是电量百分比,负数表示读不到(不显示数字)。
lv_obj_t *pupu_ui_home_create(const pupu_model_t *m, int battery, const char *notice,
                              uint32_t now_ms);
pupu_ui_req_t pupu_ui_home_action(pupu_action_t act, uint32_t now_ms);
void pupu_ui_home_update(const pupu_model_t *m, int battery, uint32_t now_ms);

// ---- 喂食页和玩耍页:宠物 + 结果气泡 + 底部选择条 ----
typedef enum { PUPU_CARE_FEED = 0, PUPU_CARE_PLAY } pupu_care_t;
lv_obj_t *pupu_ui_care_create(pupu_care_t kind, const pupu_model_t *m, uint32_t now_ms);
// acct_now 是当前的纪元记账秒(喂食要记下来,见 pupu_clock.h)。
pupu_ui_req_t pupu_ui_care_action(pupu_action_t act, pupu_model_t *m, uint32_t acct_now,
                                  uint32_t now_ms);
void pupu_ui_care_update(const pupu_model_t *m, uint32_t now_ms);
// 噗噗正在做反应(吃、开心、躲开、摇头),这时不变暗、不熄屏。
bool pupu_ui_care_busy(void);

// ---- 小游戏「接星星」(规则在 catch_model.h)----
lv_obj_t *pupu_ui_catch_create(const pupu_model_t *m, uint32_t now_ms);
// key_ms 是这个动作对应的按键时间(用来记换道延迟)。长按退出返回 PUPU_UI_GO_PLAY;
// 结果出来以后按 OK 也返回 PUPU_UI_GO_PLAY。
pupu_ui_req_t pupu_ui_catch_action(pupu_action_t act, pupu_model_t *m, uint32_t key_ms,
                                   uint32_t now_ms);
// 推进游戏、画画面;结算时返回 PUPU_UI_CHANGED。
pupu_ui_req_t pupu_ui_catch_update(pupu_model_t *m, uint32_t now_ms);
// 倒数或者游戏中:不变暗、不熄屏。
bool pupu_ui_catch_busy(void);
// 这一局还没结束(包括暂停):按键用 GAME 模式,不定时存档。
bool pupu_ui_catch_in_round(void);
// 离开这一页时调用(比如暂停时熄屏):没结束的一局作废。
void pupu_ui_catch_leave(void);

// ---- 同步页(协议见 pupu_sync_proto.h,蓝牙见 pupu_ble.h)----
// 进页面就让 pupu_ctl 打开蓝牙,开一个同步窗口;窗口开着时不能去别的页面。
// 长按 OK:先关蓝牙,关好了 update 才返回 PUPU_UI_GO_HOME。env 和 sync 在页面存在期间一直有效。
lv_obj_t *pupu_ui_sync_create(const pupu_sync_env_t *env, pupu_sync_t *sync, uint32_t now_ms);
pupu_ui_req_t pupu_ui_sync_action(pupu_action_t act, uint32_t now_ms);
// 处理蓝牙事件和手机的请求、推进窗口、画画面。收到对时(宠物状态变了)返回 PUPU_UI_CHANGED。
pupu_ui_req_t pupu_ui_sync_update(uint32_t now_ms);
// 蓝牙开着、正在开或正在关,或者噗噗在挥手:不变暗、不熄屏、不深睡。
bool pupu_ui_sync_busy(void);
// 这个窗口里对时补算的离开时长(宠物秒),回首页时显示"你回来啦"用;没有补算过返回 0。
uint64_t pupu_ui_sync_away_pet_s(void);

// ---- 状态页:饱腹、开心、星星糖,幼年多一行成长进度 ----
lv_obj_t *pupu_ui_status_create(const pupu_model_t *m);
pupu_ui_req_t pupu_ui_status_action(pupu_action_t act);
void pupu_ui_status_update(const pupu_model_t *m);

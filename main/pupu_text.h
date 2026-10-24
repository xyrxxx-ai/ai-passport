// main/pupu_text.h —— 噗噗界面上的全部文字,集中放在一处。
//
// 新加中文时:先把字加进 assets/fonts/pupu/charset.txt 并重新生成字库(见那里的 README),
// 再把字符串加进 PUPU_TEXT_LIST。tests/test_pupu_charset.py 会检查字表和字库,
// 开机时 pupu_fonts_selfcheck() 会在设备上再查一遍,缺字打印 U+XXXX。
#pragma once

#define PUPU_TXT_EGG_HINT    "按住 OK 3 秒孵化"
#define PUPU_TXT_EGG_WARMING "捂热中…"
#define PUPU_TXT_EGG_HATCHED "孵出来啦！"
#define PUPU_TXT_HUNGRY      "饿了"
#define PUPU_TXT_MENU_FEED   "喂食"
#define PUPU_TXT_MENU_PLAY   "玩耍"
#define PUPU_TXT_MENU_STATUS "状态"
#define PUPU_TXT_MENU_SYNC   "同步"

#define PUPU_TXT_BACK_HINT   "长按 OK 返回"
#define PUPU_TXT_RICE        "饭团"
#define PUPU_TXT_COOKIE      "小饼干"
#define PUPU_TXT_CANDY       "星星糖"
#define PUPU_TXT_CANDY_COUNT "星星糖 ×%d"
#define PUPU_TXT_PAT         "摸摸头"
#define PUPU_TXT_CATCH       "接星星"
#define PUPU_TXT_FULL        "饱腹"
#define PUPU_TXT_HAPPY       "开心"
#define PUPU_TXT_BOND        "亲密"
#define PUPU_TXT_GROWTH      "成长进度"
#define PUPU_TXT_TIRED       "吃腻了"
#define PUPU_TXT_REFUSED     "吃不下啦"
#define PUPU_TXT_NO_CANDY    "没有星星糖"
#define PUPU_TXT_DODGED      "别摸啦！"
#define PUPU_TXT_GREW        "长大啦！"
#define PUPU_TXT_SAVE_BROKEN "存档坏了，重新开始"
#define PUPU_TXT_SAVE_LOCKED "存档太新，这次不保存"
#define PUPU_TXT_WELCOME     "你回来啦！离开了 %u 小时"

// 小游戏「接星星」
#define PUPU_TXT_CATCH_HINT1 "↑ ↓ 换跑道"
#define PUPU_TXT_CATCH_HINT2 "接星星，躲乌云"
#define PUPU_TXT_CATCH_SECS  "%u 秒"
#define PUPU_TXT_CATCH_SCORE "%u/8"
#define PUPU_TXT_PAUSED      "暂停"
#define PUPU_TXT_PAUSE_HINT  "按 OK 继续，按住退出"
#define PUPU_TXT_CATCH_GOT   "接到 %u 颗星星"
#define PUPU_TXT_CATCH_WIN   "赢啦！"
#define PUPU_TXT_CATCH_LOSE  "差一点！"
#define PUPU_TXT_CANDY_DONE  "今天的星星糖领完啦"
#define PUPU_TXT_CANDY_STOCK "星星糖满啦"
#define PUPU_TXT_RESULT_HINT "按 OK 返回"

// 同步页
#define PUPU_TXT_SYNC_STARTING "正在打开蓝牙…"
#define PUPU_TXT_SYNC_WAITING  "等手机来连…"
#define PUPU_TXT_SYNC_HINT     "在手机上点「同步」"
#define PUPU_TXT_SYNC_SECS     "%u 秒"
#define PUPU_TXT_SYNC_LINKED   "手机连上啦"
#define PUPU_TXT_SYNC_DONE     "同步好啦！"
#define PUPU_TXT_SYNC_CLOSING  "正在关闭蓝牙…"
#define PUPU_TXT_SYNC_NO_PHONE "没等到手机"
#define PUPU_TXT_SYNC_LOST     "手机走开了"
#define PUPU_TXT_SYNC_FAILED   "蓝牙打不开"
#define PUPU_TXT_SYNC_ERROR    "蓝牙出错了"
#define PUPU_TXT_SYNC_STUCK    "蓝牙关不掉，长按再试"

#define PUPU_TEXT_LIST(X) \
    X(PUPU_TXT_EGG_HINT) X(PUPU_TXT_EGG_WARMING) X(PUPU_TXT_EGG_HATCHED) X(PUPU_TXT_HUNGRY) \
    X(PUPU_TXT_MENU_FEED) X(PUPU_TXT_MENU_PLAY) X(PUPU_TXT_MENU_STATUS) \
    X(PUPU_TXT_MENU_SYNC) X(PUPU_TXT_BACK_HINT) X(PUPU_TXT_RICE) X(PUPU_TXT_COOKIE) \
    X(PUPU_TXT_CANDY) X(PUPU_TXT_CANDY_COUNT) X(PUPU_TXT_PAT) X(PUPU_TXT_CATCH) \
    X(PUPU_TXT_FULL) X(PUPU_TXT_HAPPY) X(PUPU_TXT_BOND) X(PUPU_TXT_GROWTH) X(PUPU_TXT_TIRED) \
    X(PUPU_TXT_REFUSED) X(PUPU_TXT_NO_CANDY) X(PUPU_TXT_DODGED) X(PUPU_TXT_GREW) \
    X(PUPU_TXT_SAVE_BROKEN) X(PUPU_TXT_SAVE_LOCKED) X(PUPU_TXT_WELCOME) \
    X(PUPU_TXT_CATCH_HINT1) X(PUPU_TXT_CATCH_HINT2) X(PUPU_TXT_CATCH_SECS) \
    X(PUPU_TXT_CATCH_SCORE) X(PUPU_TXT_PAUSED) X(PUPU_TXT_PAUSE_HINT) X(PUPU_TXT_CATCH_GOT) \
    X(PUPU_TXT_CATCH_WIN) X(PUPU_TXT_CATCH_LOSE) X(PUPU_TXT_CANDY_DONE) \
    X(PUPU_TXT_CANDY_STOCK) X(PUPU_TXT_RESULT_HINT) \
    X(PUPU_TXT_SYNC_STARTING) X(PUPU_TXT_SYNC_WAITING) X(PUPU_TXT_SYNC_HINT) \
    X(PUPU_TXT_SYNC_SECS) X(PUPU_TXT_SYNC_LINKED) X(PUPU_TXT_SYNC_DONE) \
    X(PUPU_TXT_SYNC_CLOSING) X(PUPU_TXT_SYNC_NO_PHONE) X(PUPU_TXT_SYNC_LOST) \
    X(PUPU_TXT_SYNC_FAILED) X(PUPU_TXT_SYNC_ERROR) X(PUPU_TXT_SYNC_STUCK)

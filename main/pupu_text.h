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
#define PUPU_TXT_NOT_YET     "还没做好…"
#define PUPU_TXT_MENU_FEED   "喂食"
#define PUPU_TXT_MENU_PLAY   "玩耍"
#define PUPU_TXT_MENU_STATUS "状态"
#define PUPU_TXT_MENU_SYNC   "同步"

#define PUPU_TEXT_LIST(X) \
    X(PUPU_TXT_EGG_HINT) X(PUPU_TXT_EGG_WARMING) X(PUPU_TXT_EGG_HATCHED) X(PUPU_TXT_HUNGRY) \
    X(PUPU_TXT_NOT_YET) X(PUPU_TXT_MENU_FEED) X(PUPU_TXT_MENU_PLAY) X(PUPU_TXT_MENU_STATUS) \
    X(PUPU_TXT_MENU_SYNC)

// main/pupu_app.h —— 噗噗应用的入口:主循环、按键队列和页面切换。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"
#include "esp_err.h"

// 启动噗噗:建按键队列、初始化字体和宠物、显示第一个页面、开始主循环。
// 必须在 bsp_lvgl_init() 之后、持有 bsp_lvgl_lock() 时调用,只调用一次。
esp_err_t pupu_app_start(void);

// 按键回调里调用:只记下时间放进队列,马上返回。队列满时丢掉这个事件并计数。
bool pupu_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 按键唤醒守卫状态机的 host 测试:不依赖 SDK,直接验证 key_guard.h 的纯逻辑。
#include <assert.h>
#include <stdio.h>

#include "key_guard.h"

int main(void) {
    key_guard_t g = {0};

    // 未武装:不吞事件,也不改状态。
    assert(!key_guard_consume(&g, false, 0));
    assert(!g.armed);

    // 武装后:普通事件被吞,直到松手后 250 ms 或到期。时间单位是微秒。
    key_guard_arm(&g, 1000, 10000000);
    assert(g.armed);
    assert(key_guard_consume(&g, false, 1500));   // 长按被吞
    assert(g.armed);
    assert(key_guard_consume(&g, false, 2500));   // 仍被吞
    assert(g.armed);

    // 松手事件本身被吞;之后 250 ms 内迟到的单击(约 185 ms 后才到)也被吞,然后才放行。
    assert(key_guard_consume(&g, true, 2600));
    assert(g.armed);
    assert(key_guard_consume(&g, false, 2600 + 185000));                  // 迟到的单击被吞
    assert(key_guard_consume(&g, true, 2600 + KEY_GUARD_TAIL_US - 1));    // 再来一次松手不重新计时
    assert(g.armed);
    assert(!key_guard_consume(&g, false, 2600 + KEY_GUARD_TAIL_US));      // 满 250 ms 正常派发
    assert(!g.armed);

    // 到期兜底:没有松手事件也能撤防。
    key_guard_arm(&g, 1000, 10000);               // deadline = 11000
    assert(key_guard_consume(&g, false, 11001));  // 到期,吞掉这次
    assert(!g.armed);
    assert(!key_guard_consume(&g, false, 11002));

    // 释放早于初始化:按键驱动启动前就已松手,不会再有 release 事件。
    // 按采样到的物理状态(已松手)同步后立即撤防,下一次长按不再被吞。
    key_guard_arm(&g, 1000, 10000);
    key_guard_sync_released(&g, true);
    assert(!g.armed);
    assert(!key_guard_consume(&g, false, 1500));  // 下一次长按正常派发

    // 按住贯穿初始化:采样仍按住,守卫保持武装,直到松手后 250 ms。
    key_guard_arm(&g, 1000, 10000000);
    key_guard_sync_released(&g, false);           // 仍按住,不撤防
    assert(g.armed);
    assert(key_guard_consume(&g, false, 1500));   // 长按仍被吞
    assert(g.armed);
    assert(key_guard_consume(&g, true, 2600));    // 松手:开始数 250 ms
    assert(g.armed);
    assert(key_guard_consume(&g, false, 2600 + KEY_GUARD_TAIL_US - 1));
    assert(!key_guard_consume(&g, false, 2600 + KEY_GUARD_TAIL_US));
    assert(!g.armed);

    // 松手后还在吞的时候到期:撤防,那次事件仍被吞。
    key_guard_arm(&g, 0, 100000);
    assert(key_guard_consume(&g, true, 90000));
    assert(key_guard_consume(&g, false, 100000));
    assert(!g.armed);
    assert(!key_guard_consume(&g, false, 100001));

    puts("key wake guard state-machine tests: PASS");
    return 0;
}

// catch_model 的 host 测试:布局、倒数、一局的长度、暂停和退出、计分、生成规则、确定性、
// 以及用两个"机器人"做的平衡测试。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "catch_model.h"

#define LINE_Q4 (CATCH_LINE_CX * CATCH_Q4)

static void play_countdown(catch_model_t *m) {
    for (int i = 0; i < CATCH_COUNTDOWN_TICKS; i++) catch_tick(m);
    assert(m->state == CATCH_PLAYING);
}

static void test_layout(void) {
    // 顶栏在跑道上面;三条跑道在屏幕里,并且躲开底部圆角
    assert(CATCH_HUD_H <= CATCH_LANE_TOP);
    assert(CATCH_LANE_TOP + CATCH_LANES * CATCH_LANE_H <= CATCH_SCREEN_H - CATCH_CORNER_R + 6);
    assert(catch_lane_center_y(0) == 96 && catch_lane_center_y(1) == 176 &&
           catch_lane_center_y(2) == 256);
    for (int lane = 0; lane < CATCH_LANES; lane++) {
        // 噗噗和物体都在自己的跑道里
        assert(catch_pet_y(lane) >= CATCH_LANE_TOP + CATCH_LANE_H * lane);
        assert(catch_pet_y(lane) + CATCH_PET_W <= CATCH_LANE_TOP + CATCH_LANE_H * (lane + 1));
        assert(catch_item_y(lane) >= CATCH_LANE_TOP + CATCH_LANE_H * lane);
        assert(catch_item_y(lane) + CATCH_ITEM_W <= CATCH_LANE_TOP + CATCH_LANE_H * (lane + 1));
    }
    // 噗噗的左上角和左下角都在圆角外面(圆角只影响 y < 30 和 y > 290)
    assert(catch_pet_y(0) >= CATCH_CORNER_R);
    assert(catch_pet_y(CATCH_LANES - 1) + CATCH_PET_W <= CATCH_SCREEN_H - CATCH_CORNER_R);
    // 出生时整个在屏幕右边外面;接取线在噗噗身体里;离开时整个在屏幕左边外面
    assert(catch_item_x(CATCH_SPAWN_CX * CATCH_Q4) >= CATCH_SCREEN_W);
    assert(CATCH_LINE_CX > CATCH_PET_X && CATCH_LINE_CX < CATCH_PET_X + CATCH_PET_W);
    assert(catch_item_x(CATCH_GONE_CX * CATCH_Q4) + CATCH_ITEM_W <= 0);
    // 负数坐标向下取整
    assert(catch_item_x(0) == -12 && catch_item_x(15) == -12 && catch_item_x(16) == -11);
    assert(catch_item_x(-1) == -13 && catch_item_x(-16) == -13 && catch_item_x(-17) == -14);
    // 速度约 130 px/s,物体约 1.6 秒飞到接取线
    assert(catch_tuning.speed_q4 * (1000 / CATCH_TICK_MS) / CATCH_Q4 >= 120);
    assert(catch_tuning.speed_q4 * (1000 / CATCH_TICK_MS) / CATCH_Q4 <= 150);
    assert(catch_travel_ticks() == 80);
}

static void test_countdown(void) {
    catch_model_t m;
    catch_init(&m, 1);
    assert(m.state == CATCH_READY && m.lane == 1);
    assert(catch_seconds_left(&m) == 25 && catch_countdown_number(&m) == 3);
    assert(catch_active(&m) && catch_in_round(&m));
    // 倒数时可以先换道;OK 按下不暂停
    assert(catch_input(&m, CATCH_IN_UP) == CATCH_EV_LANE && m.lane == 0);
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == 0 && m.state == CATCH_READY);
    assert(catch_input(&m, CATCH_IN_OK_RELEASE) == 0 && m.state == CATCH_READY);
    for (int i = 1; i < CATCH_COUNTDOWN_TICKS; i++) {
        assert(catch_tick(&m) == 0);
        if (i == 50) assert(catch_countdown_number(&m) == 2);
        if (i == 100) assert(catch_countdown_number(&m) == 1);
    }
    assert(m.ticks == 0 && m.spawned_stars == 0);  // 倒数不算时间,也不出东西
    assert(catch_tick(&m) == CATCH_EV_START);
    assert(m.state == CATCH_PLAYING && catch_countdown_number(&m) == 0);
    assert(catch_seconds_left(&m) == 25);

    // 倒数时长按也能退出
    catch_init(&m, 1);
    catch_tick(&m);
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == 0);
    assert(catch_input(&m, CATCH_IN_OK_LONG) == CATCH_EV_EXIT && m.state == CATCH_ABORTED);
    assert(!catch_in_round(&m) && !catch_result(&m).completed);
}

static void test_round_length(void) {
    catch_model_t m;
    catch_init(&m, 7);
    play_countdown(&m);
    unsigned last_left = 25;
    for (int i = 1; i < CATCH_ROUND_TICKS; i++) {
        assert(!(catch_tick(&m) & CATCH_EV_FINISHED));
        const unsigned left = catch_seconds_left(&m);
        assert(left <= last_left && left >= 1);
        last_left = left;
    }
    assert(catch_seconds_left(&m) == 1);
    assert(catch_tick(&m) & CATCH_EV_FINISHED);  // 正好第 1250 步结束
    assert(m.state == CATCH_FINISHED && m.ticks == CATCH_ROUND_TICKS);
    assert(catch_seconds_left(&m) == 0 && !catch_in_round(&m));
    assert(catch_result(&m).completed);
    // 结束以后什么都不再变
    const catch_model_t before = m;
    assert(catch_tick(&m) == 0);
    assert(catch_input(&m, CATCH_IN_UP) == 0 && catch_input(&m, CATCH_IN_OK_LONG) == 0);
    assert(memcmp(&before, &m, sizeof(m)) == 0);
}

static void test_pause_and_resume(void) {
    catch_model_t m;
    catch_init(&m, 3);
    play_countdown(&m);
    for (int i = 0; i < 200; i++) catch_tick(&m);
    // OK 按下:立刻暂停
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == CATCH_EV_PAUSED);
    assert(m.state == CATCH_PAUSED && !catch_active(&m) && catch_in_round(&m));
    // 暂停那一下的松开不会继续
    assert(catch_input(&m, CATCH_IN_OK_RELEASE) == 0 && m.state == CATCH_PAUSED);
    // 暂停时什么都不动,也不能换道
    const catch_model_t frozen = m;
    for (int i = 0; i < 300; i++) assert(catch_tick(&m) == 0);
    assert(catch_input(&m, CATCH_IN_DOWN) == 0);
    assert(memcmp(&frozen, &m, sizeof(m)) == 0);
    // 再按一下、松开:继续
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == 0 && m.state == CATCH_PAUSED);
    assert(catch_input(&m, CATCH_IN_OK_RELEASE) == CATCH_EV_RESUMED);
    assert(m.state == CATCH_PLAYING && m.ticks == frozen.ticks);
    catch_tick(&m);
    assert(m.ticks == frozen.ticks + 1);
}

static void test_hold_ok_exits(void) {
    catch_model_t m;
    // 游戏中按住 OK:先暂停,长按触发时退出,中间不会恢复
    catch_init(&m, 5);
    play_countdown(&m);
    catch_tick(&m);
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == CATCH_EV_PAUSED);
    assert(catch_input(&m, CATCH_IN_OK_LONG) == CATCH_EV_EXIT);
    assert(m.state == CATCH_ABORTED);
    assert(catch_input(&m, CATCH_IN_OK_RELEASE) == 0 && m.state == CATCH_ABORTED);
    const catch_result_t r = catch_result(&m);
    assert(!r.completed);

    // 暂停中按住 OK:也是退出,不先恢复
    catch_init(&m, 5);
    play_countdown(&m);
    catch_input(&m, CATCH_IN_OK_PRESS);
    catch_input(&m, CATCH_IN_OK_RELEASE);
    assert(catch_input(&m, CATCH_IN_OK_PRESS) == 0);
    assert(catch_input(&m, CATCH_IN_OK_LONG) == CATCH_EV_EXIT && m.state == CATCH_ABORTED);
}

static void test_lane_clamps(void) {
    catch_model_t m;
    catch_init(&m, 9);
    play_countdown(&m);
    assert(catch_input(&m, CATCH_IN_UP) == CATCH_EV_LANE && m.lane == 0);
    assert(catch_input(&m, CATCH_IN_UP) == 0 && m.lane == 0);  // 到顶就停
    assert(catch_input(&m, CATCH_IN_DOWN) == CATCH_EV_LANE && m.lane == 1);
    assert(catch_input(&m, CATCH_IN_DOWN) == CATCH_EV_LANE && m.lane == 2);
    assert(catch_input(&m, CATCH_IN_DOWN) == 0 && m.lane == 2);
}

// 手工摆一个物体,不让它自己生成。
static void place(catch_model_t *m, int slot, catch_item_kind_t kind, int lane, int32_t cx_q4) {
    m->items[slot] = (catch_item_t){.kind = (uint8_t)kind, .lane = (uint8_t)lane, .cx_q4 = cx_q4};
}

static void test_scoring(void) {
    catch_model_t m;
    catch_init(&m, 11);
    play_countdown(&m);
    m.next_spawn = 60000;  // 这个测试里不生成
    const int sp = catch_tuning.speed_q4;

    // 同一跑道的星星:越线那一帧 +1,然后消失
    place(&m, 0, CATCH_ITEM_STAR, 1, LINE_Q4 + sp);  // 下一步正好落在线上:还没越过
    assert(catch_tick(&m) == 0 && m.items[0].cx_q4 == LINE_Q4);
    assert(catch_tick(&m) == CATCH_EV_STAR);
    assert(m.stars == 1 && m.caught == 1 && m.items[0].kind == CATCH_ITEM_NONE);

    // 别的跑道的星星:错过,继续往左飞,飞出屏幕后空出位置
    place(&m, 1, CATCH_ITEM_STAR, 0, LINE_Q4);
    assert(catch_tick(&m) == CATCH_EV_MISS && m.stars == 1);
    assert(m.items[1].kind == CATCH_ITEM_STAR && m.items[1].resolved);
    // 越线以后再换到它的跑道,也不会再算一次
    catch_input(&m, CATCH_IN_UP);
    int guard = 0;
    while (m.items[1].kind != CATCH_ITEM_NONE) {
        assert(catch_tick(&m) == 0);
        assert(++guard < 100);
    }
    assert(m.stars == 1);

    // 同一跑道的乌云:-1 颗,然后消失;最低到 0
    place(&m, 2, CATCH_ITEM_CLOUD, 0, LINE_Q4);
    assert(catch_tick(&m) == CATCH_EV_CLOUD && m.stars == 0 && m.clouds_hit == 1);
    assert(m.items[2].kind == CATCH_ITEM_NONE);
    place(&m, 2, CATCH_ITEM_CLOUD, 0, LINE_Q4);
    assert(catch_tick(&m) == CATCH_EV_CLOUD && m.stars == 0 && m.clouds_hit == 2);
    assert(m.caught == 1);  // 碰乌云不改"一共接到几颗"

    // 别的跑道的乌云:躲开了,没有事件
    place(&m, 3, CATCH_ITEM_CLOUD, 2, LINE_Q4);
    assert(catch_tick(&m) == 0 && m.items[3].resolved && m.items[3].kind == CATCH_ITEM_CLOUD);

    // 已经在线左边(比如刚才算过的)的物体不会再计分
    place(&m, 4, CATCH_ITEM_STAR, 0, LINE_Q4 - 1);
    assert(catch_tick(&m) == 0 && m.stars == 0);
}

static void test_full_pool_skips_spawn(void) {
    catch_model_t a, b;
    catch_init(&a, 21);
    play_countdown(&a);
    b = a;
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) place(&a, i, CATCH_ITEM_CLOUD, 0, 200 * CATCH_Q4);
    const uint16_t due = a.next_spawn;
    uint32_t ev = 0;
    while (a.ticks < due) ev = catch_tick(&a);
    assert(ev & CATCH_EV_FULL);
    assert(!(ev & CATCH_EV_SPAWN) && a.spawned_clouds == 0 && a.spawned_stars == 0);
    // 没生成,但随机序列和下一次出生时间照常前进
    while (b.ticks < a.ticks) catch_tick(&b);
    assert(a.rng == b.rng && a.next_spawn == b.next_spawn);
}

// 照一份按键剧本玩完一局,每一步都比较两个模型。
static void test_determinism(void) {
    catch_model_t a, b, c;
    catch_init(&a, 12345);
    catch_init(&b, 12345);
    catch_init(&c, 54321);
    bool differs = false;
    for (int step = 0; step < CATCH_COUNTDOWN_TICKS + CATCH_ROUND_TICKS; step++) {
        if (step % 37 == 0) {
            const catch_input_t in = (step / 37) % 2 ? CATCH_IN_UP : CATCH_IN_DOWN;
            catch_input(&a, in);
            catch_input(&b, in);
            catch_input(&c, in);
        }
        assert(catch_tick(&a) == catch_tick(&b));
        catch_tick(&c);
        assert(memcmp(&a, &b, sizeof(a)) == 0);
        if (memcmp(&a.items, &c.items, sizeof(a.items)) != 0) differs = true;
    }
    assert(a.state == CATCH_FINISHED && differs);
    // 种子 0 换成固定的非 0 值,随机数不会卡在 0
    catch_init(&a, 0);
    assert(a.rng != 0);
}

// ---- 机器人 ----

typedef enum { BOT_STAY, BOT_PERFECT } bot_t;

// 下一个要越线、还没算分的物体(没有就返回 NULL)。kind 为 NONE 时不限种类。
static const catch_item_t *next_item(const catch_model_t *m, catch_item_kind_t kind) {
    const catch_item_t *best = NULL;
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        const catch_item_t *it = &m->items[i];
        if (it->kind == CATCH_ITEM_NONE || it->resolved) continue;
        if (kind != CATCH_ITEM_NONE && it->kind != kind) continue;
        if (best == NULL || it->cx_q4 < best->cx_q4) best = it;
    }
    return best;
}

// "完美机器人":盯着下一个要越线的物体。是星星就去那条道;是乌云就避开它那条道,
// 尽量停在下一颗星星的道上。按键像人一样,最快每 100 ms 按一下。
static int perfect_target(const catch_model_t *m) {
    const catch_item_t *first = next_item(m, CATCH_ITEM_NONE);
    const catch_item_t *star = next_item(m, CATCH_ITEM_STAR);
    if (first == NULL) return m->lane;
    if (first->kind == CATCH_ITEM_STAR) return first->lane;
    if (star != NULL && star->lane != first->lane) return star->lane;
    if (m->lane != first->lane) return m->lane;
    return first->lane == 1 ? 0 : 1;
}

static catch_result_t play_bot(uint32_t seed, bot_t bot, unsigned *spawned_stars) {
    catch_model_t m;
    catch_init(&m, seed);
    int cooldown = 0;
    while (m.state == CATCH_READY || m.state == CATCH_PLAYING) {
        if (bot == BOT_PERFECT && cooldown-- <= 0) {
            const int target = perfect_target(&m);
            if (target != m.lane) {
                catch_input(&m, target < m.lane ? CATCH_IN_UP : CATCH_IN_DOWN);
                cooldown = 5;
            }
        }
        catch_tick(&m);
    }
    if (spawned_stars) *spawned_stars = m.spawned_stars;
    return catch_result(&m);
}

static void test_spawn_rules(void) {
    unsigned stars = 0, clouds = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        catch_model_t m;
        catch_init(&m, seed * 2654435761u);
        play_countdown(&m);
        int last_spawn = -1;
        unsigned resolved = 0;
        while (m.state == CATCH_PLAYING) {
            const uint16_t before_next = m.next_spawn;
            catch_model_t prev = m;
            const uint32_t ev = catch_tick(&m);
            assert(!(ev & CATCH_EV_FULL));  // 正常玩永远不会把 8 个位置占满
            if (ev & CATCH_EV_SPAWN) {
                assert(m.ticks == before_next);
                if (last_spawn < 0) {
                    assert(m.ticks == catch_tuning.first_spawn_tick);
                } else {
                    assert(m.ticks - last_spawn >= catch_tuning.spawn_min_ticks);
                    assert(m.ticks - last_spawn <= catch_tuning.spawn_max_ticks);
                }
                // 来得及在结束前飞到接取线
                assert(m.ticks + catch_travel_ticks() <= CATCH_ROUND_TICKS);
                last_spawn = m.ticks;
            }
            for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
                const bool was = prev.items[i].kind != CATCH_ITEM_NONE && !prev.items[i].resolved;
                // 接住的星星消失后,同一步里新出生的物体可能正好用了这个位置
                const bool now = m.items[i].kind == CATCH_ITEM_NONE || m.items[i].resolved ||
                                 m.items[i].cx_q4 == CATCH_SPAWN_CX * CATCH_Q4;
                if (was && now) resolved++;
            }
        }
        // 每个物体都在结束前算过分
        assert(resolved == (unsigned)m.spawned_stars + m.spawned_clouds);
        stars += m.spawned_stars;
        clouds += m.spawned_clouds;
    }
    const unsigned pct = clouds * 100 / (stars + clouds);
    printf("catch: 200 rounds, %u stars and %u clouds spawned (%u%% clouds)\n", stars, clouds, pct);
    assert(pct >= 15 && pct <= 25);
}

// 平衡:完美机器人每局都赢;一直待在中间不动的机器人大多数局赢不了(否则星星糖等于白送)。
static void test_balance(void) {
    unsigned perfect_min = 255, stay_wins = 0, min_spawned = 255, max_spawned = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        const uint32_t s = seed * 2654435761u;
        unsigned spawned = 0;
        const catch_result_t p = play_bot(s, BOT_PERFECT, &spawned);
        assert(p.completed);
        if (p.stars < perfect_min) perfect_min = p.stars;
        if (spawned < min_spawned) min_spawned = spawned;
        if (spawned > max_spawned) max_spawned = spawned;
        const catch_result_t idle = play_bot(s, BOT_STAY, NULL);
        if (idle.stars >= 8) stay_wins++;
    }
    printf("catch: perfect bot min %u stars; idle bot wins %u/200; %u-%u stars spawned per round\n",
           perfect_min, stay_wins, min_spawned, max_spawned);
    assert(perfect_min >= 8);
    assert(stay_wins * 100 / 200 <= 25);
}

int main(void) {
    test_layout();
    test_countdown();
    test_round_length();
    test_pause_and_resume();
    test_hold_ok_exits();
    test_lane_clamps();
    test_scoring();
    test_full_pool_skips_spawn();
    test_determinism();
    test_spawn_rules();
    test_balance();
    puts("catch model tests: PASS");
    return 0;
}

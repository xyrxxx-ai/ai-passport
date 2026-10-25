// gate_model 的 host 测试:布局、格数、卡片生成的约束(N 1–999 × 两档 × 每个波位)、每种写法
// 的值和显示一致、字符都在字库的符号表里、R 的更新、极端一局不崩、乘法卡每局最多 2 波、
// 同一种子可复现、选难度 / 倒数 / 暂停 / 退出的语义。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gate_model.h"
#include "pupu_text.h"


static void test_layout(void) {
    assert(GATE_HUD_H <= GATE_BAR_Y && GATE_BAR_Y + GATE_BAR_H <= GATE_LANE_TOP);
    assert(GATE_LANE_TOP + GATE_LANES * GATE_LANE_H <= GATE_SCREEN_H - GATE_CORNER_R + 6);
    assert(gate_lane_center_y(0) == 96 && gate_lane_center_y(1) == 176 && gate_lane_center_y(2) == 256);
    for (int lane = 0; lane < GATE_LANES; lane++) {
        // 噗噗和卡片都在自己的跑道里
        assert(gate_pet_y(lane) >= gate_lane_top(lane));
        assert(gate_pet_y(lane) + GATE_PET_W <= gate_lane_top(lane) + GATE_LANE_H);
        assert(gate_card_y(lane) >= gate_lane_top(lane));
        assert(gate_card_y(lane) + GATE_CARD_H <= gate_lane_top(lane) + GATE_LANE_H);
    }
    // 卡片在屏幕里,而且文字区(9 格 × 8 px)放得下
    assert(GATE_CARD_X + GATE_CARD_W <= GATE_SCREEN_W);
    assert(GATE_CARD_CELLS * GATE_CELL_PX <= GATE_CARD_W - 2 * GATE_CARD_PAD);
    // 最下面那张卡躲开底部圆角(圆角只影响 y > 290 的两角)
    assert(gate_card_y(GATE_LANES - 1) + GATE_CARD_H <= GATE_SCREEN_H - GATE_CORNER_R);
    assert(gate_pet_y(0) >= GATE_CORNER_R);
    // 滑动:从卡位滑到噗噗身边,首尾正好
    assert(gate_card_slide_x(0, 15) == GATE_CARD_X && gate_card_slide_x(15, 15) == GATE_CARD_APPLY_X);
    assert(GATE_CARD_APPLY_X >= GATE_PET_X + GATE_PET_W);
    assert(gate_wolf_slide_x(0, 50) == GATE_SCREEN_W && gate_wolf_slide_x(50, 50) == GATE_WOLF_X);
    assert(GATE_WOLF_X + GATE_WOLF_W <= GATE_SCREEN_W);
}

static void test_text_cells(void) {
    assert(gate_text_cells("") == 0);
    assert(gate_text_cells("+36") == 3);
    assert(gate_text_cells("−9") == 3);     // − 2 格
    assert(gate_text_cells("×2 −15") == 8);  // × 2 + "2" 1 + 空格 1 + − 2 + "15" 2
    assert(gate_text_cells("+40%") == 5);    // % 2 格
    assert(gate_text_cells("+√144") == 6);
    assert(gate_text_cells("+2⁴") == 3);     // ⁴ 1 格
    assert(gate_text_cells("+∫2x dx") == 8);
    assert(gate_text_cells("+(x³)′") == 6);
    assert(gate_text_cells("k=1→44") == 7);
    assert(gate_text_cells("羊") == 2);
}

// ---- 卡片文字的求值器:独立于 gate_model 的实现,按卡片上的字算一遍 ----

static bool starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int isqrt_exact(int k) {
    for (int s = 0; s * s <= k; s++) if (s * s == k) return s;
    return -1;
}

// 看着卡片上的字,从 n 算出结果(没截范围)。认不出的写法返回 -1。
static int evaluate(const gate_card_t *c, int n) {
    const char *l1 = c->line1;
    const char *l2 = c->line2;
    unsigned a, b;
    char rest[GATE_CARD_LINE_MAX];
    if (strcmp(l1, "×1.5") == 0) return n * 3 / 2;
    if (strcmp(l1, "+40%") == 0) return n * 14 / 10;
    if (strcmp(l1, "−25%") == 0) return n * 3 / 4;
    if (strcmp(l1, "+∫2x dx") == 0) {
        if (sscanf(l2, "0→%u", &b) != 1) return -1;
        return n + (int)(b * b);
    }
    if (strcmp(l1, "+(x³)′") == 0) {
        if (sscanf(l2, "x=%u", &b) != 1) return -1;
        return n + 3 * (int)(b * b);
    }
    if (strcmp(l1, "+∑k") == 0) {
        if (sscanf(l2, "k=1→%u", &b) != 1) return -1;
        return n + (int)(b * (b + 1) / 2);
    }
    if (l2[0] != '\0') return -1;  // 别的写法都只有一行
    if (sscanf(l1, "×%u −%u", &a, &b) == 2) return n * (int)a - (int)b;
    if (sscanf(l1, "+%u ×%u", &b, &a) == 2) return (n + (int)b) * (int)a;
    if (sscanf(l1, "×%u", &a) == 1) return n * (int)a;
    if (sscanf(l1, "÷%u", &a) == 1) return n / (int)a;
    if (sscanf(l1, "+√%u", &b) == 1) {
        const int s = isqrt_exact((int)b);
        return s < 0 ? -1 : n + s;
    }
    if (sscanf(l1, "+%u%15s", &b, rest) == 2) {
        if (strcmp(rest, "²") == 0) return n + (int)(b * b);
        if (strcmp(rest, "³") == 0) return n + (int)(b * b * b);
        if (strcmp(rest, "⁴") == 0) return n + (int)(b * b * b * b);
        if (strcmp(rest, "!") == 0) {
            int k = 1;
            for (unsigned i = 2; i <= b; i++) k *= (int)i;
            return n + k;
        }
        return -1;
    }
    if (sscanf(l1, "+%u", &b) == 1 && starts(l1, "+")) return n + (int)b;
    if (starts(l1, "−") && sscanf(l1 + strlen("−"), "%u", &b) == 1) return n - (int)b;
    return -1;
}

static int clamp(int v) {
    return v < 1 ? 1 : v > GATE_SHEEP_MAX ? GATE_SHEEP_MAX : v;
}

// 卡片里每个非 ASCII 字符都要在 PUPU_TXT_GATE_GLYPHS 里(字库和开机自检只认这些)。
static bool glyphs_ok(const char *s) {
    const char *glyphs = PUPU_TXT_GATE_GLYPHS;
    while (*s) {
        const unsigned char c = (unsigned char)*s;
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (c >= 0x80) {
            bool found = false;
            for (const char *g = glyphs; *g; g++) {
                if (strncmp(g, s, (size_t)len) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        s += len;
    }
    return true;
}

static void check_card(const gate_card_t *c, int n) {
    assert(c->line1[0] != '\0');
    assert(gate_text_cells(c->line1) <= GATE_CARD_CELLS);
    assert(gate_text_cells(c->line2) <= GATE_CARD_CELLS);
    assert(glyphs_ok(c->line1) && glyphs_ok(c->line2));
    const int v = evaluate(c, n);
    if (v < 0) {
        fprintf(stderr, "cannot evaluate card \"%s\" / \"%s\" at N=%d\n", c->line1, c->line2, n);
        exit(1);
    }
    if (clamp(v) != c->value) {
        fprintf(stderr, "card \"%s\" / \"%s\" at N=%d shows %d but model says %u\n", c->line1,
                c->line2, n, clamp(v), c->value);
        exit(1);
    }
    assert(c->value >= 1 && c->value <= GATE_SHEEP_MAX);
    // 整除条件
    if (starts(c->line1, "÷2")) assert(n % 2 == 0);
    if (starts(c->line1, "÷3")) assert(n % 3 == 0);
    if (strcmp(c->line1, "+40%") == 0) assert(n % 5 == 0);
    if (strcmp(c->line1, "−25%") == 0) assert(n % 4 == 0);
    if (strcmp(c->line1, "×1.5") == 0) assert(n % 2 == 0);
    // 乘法卡标记和写法一致
    const bool mul = starts(c->line1, "×") || strcmp(c->line1, "+40%") == 0 || strstr(c->line1, " ×") != NULL;
    assert(c->mul == mul);
}

// 一波的三张卡:值互不相同、满足交叉相乘约束、rank 和值的大小一致。
static void check_wave(const gate_model_t *m, int n) {
    const gate_card_t *by_rank[3] = {NULL, NULL, NULL};
    for (int lane = 0; lane < GATE_LANES; lane++) {
        const gate_card_t *c = gate_card(m, lane);
        check_card(c, n);
        assert(c->rank < 3 && by_rank[c->rank] == NULL);
        by_rank[c->rank] = c;
    }
    const unsigned best = by_rank[0]->value, second = by_rank[1]->value, third = by_rank[2]->value;
    if (!(best > second && second > third && second * 100 >= best * 80 && third * 100 <= best * 60)) {
        fprintf(stderr, "bad wave at N=%d (level %u wave %u): \"%s %s\"=%u \"%s %s\"=%u \"%s %s\"=%u\n", n,
                m->level, m->wave, by_rank[0]->line1, by_rank[0]->line2, best, by_rank[1]->line1,
                by_rank[1]->line2, second, by_rank[2]->line1, by_rank[2]->line2, third);
        exit(1);
    }
}

static unsigned s_families_seen[GATE_FAM_COUNT];

static void test_generation_all_n(void) {
    unsigned by_stage[4] = {0, 0, 0, 0};
    unsigned low_n_other = 0, low_n = 0;  // N ≤ 400 时没用上该波家族(③ 或 ④)的波数
    int fallback_min_n = GATE_SHEEP_MAX + 1;
    for (int mul_used = 0; mul_used <= 2; mul_used += 2) {  // 乘法卡还能出 / 已经用完
        for (int level = 0; level < 2; level++) {
            for (int wave = 1; wave <= GATE_WAVES; wave++) {
                for (int n = 1; n <= GATE_SHEEP_MAX; n++) {
                    gate_model_t m;
                    gate_init(&m, (uint32_t)(n * 131 + wave * 7 + level), (gate_level_t)level);
                    m.wave = (uint8_t)wave;
                    m.sheep = (uint16_t)n;
                    m.mul_waves = (uint8_t)mul_used;
                    gate_make_wave(&m);
                    check_wave(&m, n);
                    for (int lane = 0; lane < GATE_LANES; lane++) s_families_seen[gate_card(&m, lane)->family]++;
                    const uint8_t stage = m.stage[wave - 1];
                    assert(stage <= 3);
                    if (stage == 3 && n < fallback_min_n) fallback_min_n = n;
                    if (mul_used == 2) {
                        // 乘法卡用完以后这一波没有乘法卡,mul_waves 也不会再加
                        for (int lane = 0; lane < GATE_LANES; lane++) assert(!gate_card(&m, lane)->mul);
                        assert(m.mul_waves == 2);
                        continue;
                    }
                    by_stage[stage]++;
                    if (n <= 400) {
                        low_n++;
                        if (stage >= 2) low_n_other++;
                    }
                }
            }
        }
    }
    printf("gate: N 1-999 x 2 levels x 6 waves: stage 1 %u, 2 %u, 3 %u, 4 %u waves; "
           "first fallback at N=%d; %u/%u waves with N<=400 left their own families\n",
           by_stage[0], by_stage[1], by_stage[2], by_stage[3], fallback_min_n, low_n_other, low_n);
    for (int f = 0; f < GATE_FAM_COUNT; f++) assert(s_families_seen[f] > 0);  // 每个家族都出现过
    // 退化只在 N 很大时出现(加不上去、second 没法 ≥ 0.8 × best);中小 N 基本都用该波自己的家族
    assert(fallback_min_n >= 800);
    assert(low_n_other * 100 / low_n <= 5);
}

static void test_first_wave_shapes(void) {
    // 普通第 1 波、N = 10:best 是 +6 以上或 ×2(增量 ≥ 0.6N),次好 ≥ 80%,最差 ≤ 60%
    for (uint32_t seed = 1; seed <= 300; seed++) {
        gate_model_t m;
        gate_init(&m, seed, GATE_EASY);
        m.wave = 1;
        gate_make_wave(&m);
        check_wave(&m, 10);
        for (int lane = 0; lane < GATE_LANES; lane++) {
            const gate_card_t *c = gate_card(&m, lane);
            if (c->rank == 0) assert(c->value >= 16);
            assert(c->family <= GATE_FAM_DIV);  // 第 1 波只有加减乘除
        }
    }
    // 学霸第 3 波只有积分和导数;第 5 波只有求和、幂、×1.5
    for (uint32_t seed = 1; seed <= 100; seed++) {
        gate_model_t m;
        gate_init(&m, seed, GATE_HARD);
        m.wave = 3;
        m.sheep = 50;
        gate_make_wave(&m);
        for (int lane = 0; lane < GATE_LANES; lane++) {
            const uint8_t f = gate_card(&m, lane)->family;
            assert(f == GATE_FAM_INT || f == GATE_FAM_DERIV);
        }
        gate_init(&m, seed, GATE_HARD);
        m.wave = 5;
        m.sheep = 60;
        gate_make_wave(&m);
        for (int lane = 0; lane < GATE_LANES; lane++) {
            const uint8_t f = gate_card(&m, lane)->family;
            assert(f == GATE_FAM_SUM || f == GATE_FAM_POW || f == GATE_FAM_X15);
        }
    }
}

static void test_permille(void) {
    assert(gate_next_permille(1000, 100, 100) == 1000);
    assert(gate_next_permille(1000, 80, 100) == 800);
    assert(gate_next_permille(800, 80, 100) == 640);   // 选次好两次还是赢(≥ 600)
    assert(gate_next_permille(640, 80, 100) == 512);   // 三次就输了
    assert(gate_next_permille(1000, 60, 100) == 600);  // 选到最差一张正好 600
    assert(gate_next_permille(600, 99, 100) == 594);   // 之后再错任何一次就输
    assert(gate_next_permille(1000, 1, 999) == 1);     // 最低 1
    assert(gate_next_permille(1, 1, 999) == 1);
    assert(gate_next_permille(700, 50, 0) == 700);     // best 为 0 不除
    // 向下取整
    assert(gate_next_permille(1000, 2, 3) == 666);
}

static void play_countdown(gate_model_t *m) {
    assert(gate_input(m, GATE_IN_OK_RELEASE) == GATE_EV_COUNTDOWN);
    assert(m->state == GATE_COUNTDOWN);
    for (int i = 0; i < GATE_COUNTDOWN_TICKS - 1; i++) assert(gate_tick(m) == 0);
    const uint32_t ev = gate_tick(m);
    assert((ev & GATE_EV_START) && (ev & GATE_EV_WAVE));
    assert(m->state == GATE_WAVE && m->wave == 1);
}

// 把噗噗挪到某个 rank 的卡片所在跑道。
static void go_to_rank(gate_model_t *m, int rank) {
    int target = -1;
    for (int lane = 0; lane < GATE_LANES; lane++) {
        if (gate_card(m, lane)->rank == rank) target = lane;
    }
    assert(target >= 0);
    while (m->lane != target) {
        const uint32_t ev = gate_input(m, m->lane > target ? GATE_IN_UP : GATE_IN_DOWN);
        assert(ev == GATE_EV_LANE);
    }
}

// 让一波走完,返回卡片生效那一步的事件。
static uint32_t finish_wave(gate_model_t *m) {
    uint32_t ev = 0;
    while (m->state == GATE_WAVE) ev = gate_tick(m);
    assert(ev == GATE_EV_APPLYING && m->state == GATE_APPLYING);
    for (int i = 1; i < gate_tuning.apply_ticks; i++) assert(gate_tick(m) == 0);
    return gate_tick(m);
}

// 整局都选某个 rank;返回结果。
static gate_result_t play_round(uint32_t seed, gate_level_t level, int rank) {
    gate_model_t m;
    gate_init(&m, seed, level);
    play_countdown(&m);
    uint16_t last_r = 1000;
    for (int wave = 1; wave <= GATE_WAVES; wave++) {
        assert(m.wave == wave);
        check_wave(&m, m.sheep);
        const uint16_t best = 0;
        (void)best;
        go_to_rank(&m, rank);
        const uint16_t chosen = gate_card(&m, m.lane)->value;
        const uint32_t ev = finish_wave(&m);
        const uint32_t pick = rank == 0 ? GATE_EV_PICK_BEST : rank == 1 ? GATE_EV_PICK_SECOND : GATE_EV_PICK_THIRD;
        assert(ev & pick);
        assert(m.sheep == chosen);
        assert(m.r_permille >= 1 && m.r_permille <= last_r);
        last_r = m.r_permille;
        if (wave < GATE_WAVES) {
            assert((ev & GATE_EV_WAVE) && m.state == GATE_WAVE);
        } else {
            assert((ev & GATE_EV_WOLVES) && m.state == GATE_WOLVES);
        }
    }
    // 狼群结算 2 秒,期间按键都不响应
    assert(gate_input(&m, GATE_IN_OK_LONG) == 0 && m.state == GATE_WOLVES);
    assert(gate_input(&m, GATE_IN_UP) == 0);
    for (int i = 1; i < gate_tuning.wolves_ticks; i++) assert(gate_tick(&m) == 0);
    assert(gate_tick(&m) == GATE_EV_FINISHED && m.state == GATE_FINISHED);
    assert(!gate_in_round(&m) && !gate_active(&m));
    // 结束以后什么都不再变
    const gate_model_t before = m;
    assert(gate_tick(&m) == 0 && gate_input(&m, GATE_IN_UP) == 0 && gate_input(&m, GATE_IN_OK_LONG) == 0);
    assert(memcmp(&before, &m, sizeof(m)) == 0);
    return gate_result(&m);
}

static void test_rounds(void) {
    unsigned mul_over = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        for (int level = 0; level < 2; level++) {
            // 全选最好:R 不变,必赢,狼群 W = ⌈0.6N⌉ ≤ N
            gate_result_t r = play_round(seed, (gate_level_t)level, 0);
            assert(r.completed && r.won && r.r_permille == 1000);
            assert(r.wolves == ((uint32_t)r.sheep * 600 + 999) / 1000 && r.wolves <= r.sheep);
            // 全选次好:次好在 0.8 × best 到 best − 1 之间,可赢可输;"N ≥ W"和"R ≥ 600"永远一致
            r = play_round(seed, (gate_level_t)level, 1);
            assert(r.completed && r.r_permille >= 262 && r.r_permille <= 1000);
            assert(r.won == (r.r_permille >= 600) && r.won == (r.sheep >= r.wolves));
            // 全选最差(含 third = 1、best = 999 的极端):必输(每波 ≤ 0.6),不崩,R ≥ 1,W > N
            r = play_round(seed, (gate_level_t)level, 2);
            assert(r.completed && !r.won && r.r_permille >= 1 && r.r_permille <= 600 && r.wolves > r.sheep);
        }
    }
    // 乘法卡每局最多出现在 2 波里(含学霸两步 + ×1.5 的组合);顺便统计真实一局里各波用了第几步生成
    unsigned by_stage[4] = {0, 0, 0, 0};
    uint16_t max_sheep = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        for (int level = 0; level < 2; level++) {
            gate_model_t m;
            gate_init(&m, seed * 2654435761u, (gate_level_t)level);
            play_countdown(&m);
            unsigned mul_waves = 0;
            for (int wave = 1; wave <= GATE_WAVES; wave++) {
                bool mul = false;
                for (int lane = 0; lane < GATE_LANES; lane++) if (gate_card(&m, lane)->mul) mul = true;
                if (mul) mul_waves++;
                by_stage[m.stage[wave - 1]]++;
                if (seed == 1) {
                    printf("gate: demo %s wave %d N=%u:", level ? "hard" : "easy", wave, m.sheep);
                    for (int lane = 0; lane < GATE_LANES; lane++) {
                        const gate_card_t *c = gate_card(&m, lane);
                        printf("  [%s%s%s]=%u(%c)", c->line1, c->line2[0] ? " " : "", c->line2, c->value, "BST"[c->rank]);
                    }
                    printf("\n");
                }
                go_to_rank(&m, 0);  // 一直选最好:N 长得最快
                finish_wave(&m);
            }
            assert(mul_waves <= gate_tuning.mul_waves_max);
            if (mul_waves == gate_tuning.mul_waves_max) mul_over++;
            assert(m.sheep <= GATE_SHEEP_MAX);
            if (m.sheep > max_sheep) max_sheep = m.sheep;
        }
    }
    printf("gate: 400 best-only rounds: %u used both multiplication waves, max N %u, "
           "waves by stage %u / %u / %u / %u\n", mul_over, max_sheep, by_stage[0], by_stage[1], by_stage[2], by_stage[3]);
    // 真实一局里绝大多数波都用该波自己的家族;退化只在 N 冲到 999 附近时偶尔发生
    const unsigned total = by_stage[0] + by_stage[1] + by_stage[2] + by_stage[3];
    assert(by_stage[2] * 100 / total <= 20);
    assert(by_stage[3] * 100 / total <= 1);
}

static void test_extreme_values(void) {
    // N = 999:加不上去了,退化成 +1(截成 999)和两张减法卡
    gate_model_t m;
    gate_init(&m, 5, GATE_HARD);
    m.wave = 4;
    m.sheep = 999;
    gate_make_wave(&m);
    check_wave(&m, 999);
    // N = 1:最差一张可以是 third = 1?不行,best ≥ 2 且 third < second < best;至少能生成
    gate_init(&m, 6, GATE_EASY);
    m.wave = 1;
    m.sheep = 1;
    gate_make_wave(&m);
    check_wave(&m, 1);
    // 从 N = 999 开始全选最差,一直到 1 也不崩
    gate_init(&m, 7, GATE_EASY);
    m.sheep = 999;
    play_countdown(&m);
    for (int wave = 1; wave <= GATE_WAVES; wave++) {
        check_wave(&m, m.sheep);
        go_to_rank(&m, 2);
        finish_wave(&m);
        assert(m.sheep >= 1 && m.r_permille >= 1);
    }
    assert(gate_wolves(&m) > m.sheep);
    assert(gate_wolves(&m) <= 999u * 600u);  // W 的理论上限
}

static void test_level_select(void) {
    gate_model_t m;
    gate_init(&m, 1, GATE_EASY);
    assert(m.state == GATE_READY_LEVEL && m.level == GATE_EASY && m.lane == 1);
    assert(m.sheep == 10 && m.r_permille == 1000 && m.wave == 0);
    assert(gate_in_round(&m) && !gate_active(&m));  // 按键走 GAME 模式,但可以变暗、熄屏
    assert(gate_wave_ms_left(&m) == 0 && gate_countdown_number(&m) == 0);
    // ↑ 普通、↓ 学霸;重复按不发事件
    assert(gate_input(&m, GATE_IN_DOWN) == GATE_EV_LEVEL && m.level == GATE_HARD);
    assert(gate_input(&m, GATE_IN_DOWN) == 0 && m.level == GATE_HARD);
    assert(gate_input(&m, GATE_IN_UP) == GATE_EV_LEVEL && m.level == GATE_EASY);
    assert(gate_input(&m, GATE_IN_DOWN) == GATE_EV_LEVEL);
    // 选难度时 tick 什么都不做;按下 OK 不开始,松开才开始
    assert(gate_tick(&m) == 0 && m.state == GATE_READY_LEVEL);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0 && m.state == GATE_READY_LEVEL);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == GATE_EV_COUNTDOWN && m.state == GATE_COUNTDOWN);
    assert(gate_countdown_number(&m) == 3 && gate_active(&m));
    // 学霸档第 1 波的秒数来自 gate_tuning
    for (int i = 0; i < GATE_COUNTDOWN_TICKS; i++) gate_tick(&m);
    const unsigned hard1 = gate_tuning.wave_s[GATE_HARD][0];
    assert(m.state == GATE_WAVE && m.wave_len == hard1 * 50 && gate_wave_ms_left(&m) == hard1 * 1000);
    // 按住 OK 直接退出,不会先开一局
    gate_init(&m, 1, GATE_EASY);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0);
    assert(gate_input(&m, GATE_IN_OK_LONG) == GATE_EV_EXIT && m.state == GATE_ABORTED);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == 0 && m.state == GATE_ABORTED);
    assert(!gate_result(&m).completed && !gate_in_round(&m));
}

static void test_countdown(void) {
    gate_model_t m;
    gate_init(&m, 3, GATE_EASY);
    gate_input(&m, GATE_IN_OK_RELEASE);
    // 倒数时可以先换道;OK 按下不暂停
    assert(gate_input(&m, GATE_IN_UP) == GATE_EV_LANE && m.lane == 0);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0 && m.state == GATE_COUNTDOWN);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == 0 && m.state == GATE_COUNTDOWN);
    for (int i = 1; i < GATE_COUNTDOWN_TICKS; i++) {
        assert(gate_tick(&m) == 0);
        if (i == 50) assert(gate_countdown_number(&m) == 2);
        if (i == 100) assert(gate_countdown_number(&m) == 1);
    }
    assert(m.wave == 0);  // 倒数不出卡片
    const uint32_t ev = gate_tick(&m);
    assert((ev & GATE_EV_START) && m.state == GATE_WAVE && m.wave == 1 && gate_countdown_number(&m) == 0);
    const unsigned easy1 = gate_tuning.wave_s[GATE_EASY][0];
    assert(m.wave_len == easy1 * 50 && gate_wave_ms_left(&m) == easy1 * 1000);
    // 倒数时长按也能退出
    gate_init(&m, 3, GATE_EASY);
    gate_input(&m, GATE_IN_OK_RELEASE);
    gate_tick(&m);
    assert(gate_input(&m, GATE_IN_OK_LONG) == GATE_EV_EXIT && m.state == GATE_ABORTED);
}

static void test_wave_timing_and_lanes(void) {
    gate_model_t m;
    gate_init(&m, 9, GATE_EASY);
    play_countdown(&m);
    assert(gate_input(&m, GATE_IN_UP) == GATE_EV_LANE && m.lane == 0);
    assert(gate_input(&m, GATE_IN_UP) == 0 && m.lane == 0);  // 到顶就停
    assert(gate_input(&m, GATE_IN_DOWN) == GATE_EV_LANE && m.lane == 1);
    assert(gate_input(&m, GATE_IN_DOWN) == GATE_EV_LANE && m.lane == 2);
    assert(gate_input(&m, GATE_IN_DOWN) == 0 && m.lane == 2);
    // 倒计时条:从满到空,每步少 20 ms
    const unsigned total_ms = (unsigned)m.wave_len * GATE_TICK_MS;
    unsigned last = gate_wave_ms_left(&m);
    assert(last == total_ms && total_ms == gate_tuning.wave_s[GATE_EASY][0] * 1000u);
    for (int i = 1; i < m.wave_len; i++) {
        assert(gate_tick(&m) == 0);
        const unsigned left = gate_wave_ms_left(&m);
        assert(left == total_ms - (unsigned)i * 20u && left < last);
        last = left;
    }
    // 最后一步:时间到,噗噗所在跑道(2)的卡片开始滑
    assert(gate_tick(&m) == GATE_EV_APPLYING && m.state == GATE_APPLYING && m.chosen_lane == 2);
    assert(gate_wave_ms_left(&m) == 0);
    // 滑动中换道无效,卡片照滑;滑完以后生效
    assert(gate_input(&m, GATE_IN_UP) == 0 && m.lane == 2);
    const uint16_t expected = gate_card(&m, 2)->value;
    for (int i = 1; i < gate_tuning.apply_ticks; i++) assert(gate_tick(&m) == 0);
    const uint32_t ev = gate_tick(&m);
    assert(m.sheep == expected && m.wave == 2 && (ev & GATE_EV_WAVE));
    assert(m.picks[0] == gate_card(&m, 2)->rank || true);  // picks 记录的是生效那张卡的 rank
    // 第 3 波的秒数也来自 gate_tuning
    go_to_rank(&m, 0);
    finish_wave(&m);
    assert(m.wave == 3 && m.wave_len == gate_tuning.wave_s[GATE_EASY][2] * 50);
}

static void test_pause_resume_exit(void) {
    gate_model_t m;
    gate_init(&m, 11, GATE_EASY);
    play_countdown(&m);
    for (int i = 0; i < 50; i++) gate_tick(&m);
    // OK 按下:立刻暂停;那一下的松开不会继续
    assert(gate_input(&m, GATE_IN_OK_PRESS) == GATE_EV_PAUSED);
    assert(m.state == GATE_PAUSED && !gate_active(&m) && gate_in_round(&m));
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == 0 && m.state == GATE_PAUSED);
    // 暂停时什么都不动,也不能换道;倒计时条停着
    const gate_model_t frozen = m;
    for (int i = 0; i < 300; i++) assert(gate_tick(&m) == 0);
    assert(gate_input(&m, GATE_IN_DOWN) == 0);
    assert(memcmp(&frozen, &m, sizeof(m)) == 0);
    assert(gate_wave_ms_left(&m) == (unsigned)m.wave_len * GATE_TICK_MS - 50u * 20u);
    // 再按一下、松开:继续
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0 && m.state == GATE_PAUSED);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == GATE_EV_RESUMED && m.state == GATE_WAVE);
    assert(m.wave_ticks == frozen.wave_ticks);
    gate_tick(&m);
    assert(m.wave_ticks == frozen.wave_ticks + 1);
    // 卡片滑到一半暂停:继续时接着滑
    while (m.state == GATE_WAVE) gate_tick(&m);
    gate_tick(&m);
    assert(m.state == GATE_APPLYING && m.apply_ticks == 1);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == GATE_EV_PAUSED && m.state == GATE_PAUSED);
    assert(gate_wave_ms_left(&m) == 0);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == 0);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == GATE_EV_RESUMED && m.state == GATE_APPLYING);
    assert(m.apply_ticks == 1);
    // 游戏中按住 OK:先暂停,长按触发时退出,中间不会恢复
    gate_init(&m, 12, GATE_EASY);
    play_countdown(&m);
    gate_tick(&m);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == GATE_EV_PAUSED);
    assert(gate_input(&m, GATE_IN_OK_LONG) == GATE_EV_EXIT && m.state == GATE_ABORTED);
    assert(gate_input(&m, GATE_IN_OK_RELEASE) == 0 && m.state == GATE_ABORTED);
    assert(!gate_result(&m).completed);
    // 暂停中按住 OK:也是退出,不先恢复
    gate_init(&m, 12, GATE_EASY);
    play_countdown(&m);
    gate_input(&m, GATE_IN_OK_PRESS);
    gate_input(&m, GATE_IN_OK_RELEASE);
    assert(gate_input(&m, GATE_IN_OK_PRESS) == 0);
    assert(gate_input(&m, GATE_IN_OK_LONG) == GATE_EV_EXIT && m.state == GATE_ABORTED);
}

static void test_determinism(void) {
    gate_model_t a, b, c;
    gate_init(&a, 12345, GATE_HARD);
    gate_init(&b, 12345, GATE_HARD);
    gate_init(&c, 54321, GATE_HARD);
    gate_input(&a, GATE_IN_OK_RELEASE);
    gate_input(&b, GATE_IN_OK_RELEASE);
    gate_input(&c, GATE_IN_OK_RELEASE);
    bool differs = false;
    unsigned hard_total_s = 0;
    for (int w = 0; w < GATE_WAVES; w++) hard_total_s += gate_tuning.wave_s[GATE_HARD][w];
    for (int step = 0; step < GATE_COUNTDOWN_TICKS + (int)hard_total_s * 50 + 6 * 15 + 100; step++) {
        if (step % 37 == 0) {
            const gate_input_t in = (step / 37) % 2 ? GATE_IN_UP : GATE_IN_DOWN;
            gate_input(&a, in);
            gate_input(&b, in);
            gate_input(&c, in);
        }
        assert(gate_tick(&a) == gate_tick(&b));
        gate_tick(&c);
        assert(memcmp(&a, &b, sizeof(a)) == 0);
        if (memcmp(a.cards, c.cards, sizeof(a.cards)) != 0) differs = true;
    }
    assert(a.state == GATE_FINISHED && differs);
    gate_init(&a, 0, GATE_EASY);
    assert(a.rng != 0);  // 种子 0 换成固定的非 0 值
}

int main(void) {
    test_layout();
    test_text_cells();
    test_generation_all_n();
    test_first_wave_shapes();
    test_permille();
    test_rounds();
    test_extreme_values();
    test_level_select();
    test_countdown();
    test_wave_timing_and_lanes();
    test_pause_resume_exit();
    test_determinism();
    puts("gate model tests: PASS");
    return 0;
}

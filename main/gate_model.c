#include "gate_model.h"

#include <stdio.h>
#include <string.h>

const gate_tuning_t gate_tuning = {
    .wave_s = { {4, 4, 6, 6, 6, 6}, {6, 6, 8, 8, 8, 8} },
    .sheep_start = 10,
    .win_permille = 600,
    .second_min_pct = 80,
    .third_max_pct = 60,
    .apply_ticks = 15,
    .wolves_ticks = 100,
    .mul_waves_max = 2,
};

#define SEED_FALLBACK 0x9E3779B9u
#define TICKS_PER_S (1000 / GATE_TICK_MS)
#define TWO_STEP_B_MAX 99  // 两步卡里的常数最多两位数,文字才放得下

// 每波允许的家族(位掩码)。两步 = ×a −b、+b ×a、+40%、−25%、×1.5。
#define F(fam) (1u << (fam))
#define FAMS_TWO_STEP (F(GATE_FAM_MUL_SUB) | F(GATE_FAM_ADD_MUL) | F(GATE_FAM_P40) | F(GATE_FAM_M25) | F(GATE_FAM_X15))
#define FAMS_ALL ((1u << GATE_FAM_COUNT) - 1u)
static const uint32_t FAMILIES[2][3] = {
    // 普通:第 1–2 波 加减乘除,第 3–4 波 两步,第 5–6 波 平方根、幂、阶乘
    { F(GATE_FAM_ADD) | F(GATE_FAM_SUB) | F(GATE_FAM_MUL) | F(GATE_FAM_DIV), FAMS_TWO_STEP,
      F(GATE_FAM_SQRT) | F(GATE_FAM_POW) | F(GATE_FAM_FACT) },
    // 学霸:第 1–2 波 两步,第 3–4 波 积分、导数,第 5–6 波 求和、幂、×1.5
    { FAMS_TWO_STEP, F(GATE_FAM_INT) | F(GATE_FAM_DERIV),
      F(GATE_FAM_SUM) | F(GATE_FAM_POW) | F(GATE_FAM_X15) },
};

// 一种写法:家族 + 参数。a 是乘数 / 指数 / 阶乘的 n,b 是加数、减数、底数、上限等。
typedef struct {
    uint8_t fam;
    uint8_t a;
    uint16_t b;
} writing_t;

static uint32_t next_rand(gate_model_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}

// [0, n) 里的一个随机整数。
static uint32_t rand_below(gate_model_t *m, uint32_t n) {
    return n <= 1 ? 0 : next_rand(m) % n;
}

static int clamp_sheep(int v) {
    if (v < 1) return 1;
    if (v > GATE_SHEEP_MAX) return GATE_SHEEP_MAX;
    return v;
}

static int ceil_pct(int v, int pct) {
    return (v * pct + 99) / 100;
}

// 加法类家族(加减、平方根、幂、阶乘、积分、导数、求和)的加数上限 max(⌊1.2N⌋, 6)。
static int add_max(int n) {
    const int k = n * 12 / 10;
    return k > 6 ? k : 6;
}

static bool is_mul(uint8_t fam) {
    return fam == GATE_FAM_MUL || fam == GATE_FAM_MUL_SUB || fam == GATE_FAM_ADD_MUL ||
           fam == GATE_FAM_P40 || fam == GATE_FAM_X15;
}

// 一种写法在 N 上算出多少(没截范围;从左到右依次算,没有括号)。
static int writing_value(const writing_t *w, int n) {
    switch (w->fam) {
    case GATE_FAM_ADD: return n + w->b;
    case GATE_FAM_SUB: return n - w->b;
    case GATE_FAM_MUL: return n * w->a;
    case GATE_FAM_DIV: return n / w->a;
    case GATE_FAM_MUL_SUB: return n * w->a - w->b;
    case GATE_FAM_ADD_MUL: return (n + w->b) * w->a;
    case GATE_FAM_P40: return n * 14 / 10;
    case GATE_FAM_M25: return n * 3 / 4;
    case GATE_FAM_X15: return n * 3 / 2;
    case GATE_FAM_SQRT: return n + w->b;             // 卡上写 √(b²),值是 b
    case GATE_FAM_POW: {                             // a 是指数,b 是底数
        int k = 1;
        for (int i = 0; i < w->a; i++) k *= w->b;
        return n + k;
    }
    case GATE_FAM_FACT: {                            // a 是 n!
        int k = 1;
        for (int i = 2; i <= w->a; i++) k *= i;
        return n + k;
    }
    case GATE_FAM_INT: return n + w->b * w->b;       // ∫₀ᵇ 2x dx = b²
    case GATE_FAM_DERIV: return n + 3 * w->b * w->b; // (x³)′ 在 x=b 处 = 3b²
    case GATE_FAM_SUM: return n + w->b * (w->b + 1) / 2;  // ∑ k, k=1..b
    default: return n;
    }
}

// 把一种写法写成卡片上的两行字。
static void writing_text(const writing_t *w, char *line1, char *line2) {
    line2[0] = '\0';
    switch (w->fam) {
    case GATE_FAM_ADD: snprintf(line1, GATE_CARD_LINE_MAX, "+%u", w->b); break;
    case GATE_FAM_SUB: snprintf(line1, GATE_CARD_LINE_MAX, "−%u", w->b); break;
    case GATE_FAM_MUL: snprintf(line1, GATE_CARD_LINE_MAX, "×%u", w->a); break;
    case GATE_FAM_DIV: snprintf(line1, GATE_CARD_LINE_MAX, "÷%u", w->a); break;
    case GATE_FAM_MUL_SUB: snprintf(line1, GATE_CARD_LINE_MAX, "×%u −%u", w->a, w->b); break;
    case GATE_FAM_ADD_MUL: snprintf(line1, GATE_CARD_LINE_MAX, "+%u ×%u", w->b, w->a); break;
    case GATE_FAM_P40: snprintf(line1, GATE_CARD_LINE_MAX, "+40%%"); break;
    case GATE_FAM_M25: snprintf(line1, GATE_CARD_LINE_MAX, "−25%%"); break;
    case GATE_FAM_X15: snprintf(line1, GATE_CARD_LINE_MAX, "×1.5"); break;
    case GATE_FAM_SQRT: snprintf(line1, GATE_CARD_LINE_MAX, "+√%u", w->b * w->b); break;
    case GATE_FAM_POW:
        snprintf(line1, GATE_CARD_LINE_MAX, "+%u%s", w->b, w->a == 2 ? "²" : w->a == 3 ? "³" : "⁴");
        break;
    case GATE_FAM_FACT: snprintf(line1, GATE_CARD_LINE_MAX, "+%u!", w->a); break;
    case GATE_FAM_INT:
        snprintf(line1, GATE_CARD_LINE_MAX, "+∫2x dx");
        snprintf(line2, GATE_CARD_LINE_MAX, "0→%u", w->b);
        break;
    case GATE_FAM_DERIV:
        snprintf(line1, GATE_CARD_LINE_MAX, "+(x³)′");
        snprintf(line2, GATE_CARD_LINE_MAX, "x=%u", w->b);
        break;
    case GATE_FAM_SUM:
        snprintf(line1, GATE_CARD_LINE_MAX, "+∑k");
        snprintf(line2, GATE_CARD_LINE_MAX, "k=1→%u", w->b);
        break;
    default: line1[0] = '\0'; break;
    }
}

// 枚举 fams 里的家族在 N 上能写出的全部写法(值已经在 1–999 内),每个交给 cb。
// cb 返回 false 时停下。ctx 由调用方定。
typedef bool (*writing_cb)(void *ctx, const writing_t *w, int value);

static bool emit(writing_cb cb, void *ctx, uint8_t fam, uint8_t a, uint16_t b, int n) {
    const writing_t w = {.fam = fam, .a = a, .b = b};
    const int v = writing_value(&w, n);
    if (v < 1 || v > GATE_SHEEP_MAX) return true;  // 超范围的写法不出
    return cb(ctx, &w, v);
}

static void for_each_writing(uint32_t fams, bool allow_mul, int n, writing_cb cb, void *ctx) {
    const int kmax = add_max(n);
    for (uint8_t fam = 0; fam < GATE_FAM_COUNT; fam++) {
        if (!(fams & F(fam))) continue;
        if (!allow_mul && is_mul(fam)) continue;
        switch (fam) {
        case GATE_FAM_ADD:
            for (int k = 1; k <= kmax; k++) if (!emit(cb, ctx, fam, 0, (uint16_t)k, n)) return;
            break;
        case GATE_FAM_SUB:
            if (n >= 5) {
                for (int k = (n + 4) / 5; k <= n / 2; k++) if (!emit(cb, ctx, fam, 0, (uint16_t)k, n)) return;
            }
            break;
        case GATE_FAM_MUL:
            for (uint8_t a = 2; a <= 3; a++) if (!emit(cb, ctx, fam, a, 0, n)) return;
            break;
        case GATE_FAM_DIV:
            if (n % 2 == 0 && !emit(cb, ctx, fam, 2, 0, n)) return;
            if (n % 3 == 0 && !emit(cb, ctx, fam, 3, 0, n)) return;
            break;
        case GATE_FAM_MUL_SUB:  // ×a −b:b 不超过 (a−1)N,结果不低于 N
            for (uint8_t a = 2; a <= 3; a++) {
                const int bmax = (a - 1) * n < TWO_STEP_B_MAX ? (a - 1) * n : TWO_STEP_B_MAX;
                for (int b = 1; b <= bmax; b++) if (!emit(cb, ctx, fam, a, (uint16_t)b, n)) return;
            }
            break;
        case GATE_FAM_ADD_MUL:  // +b ×a:b 不超过 N
            for (uint8_t a = 2; a <= 3; a++) {
                const int bmax = n < TWO_STEP_B_MAX ? n : TWO_STEP_B_MAX;
                for (int b = 1; b <= bmax; b++) if (!emit(cb, ctx, fam, a, (uint16_t)b, n)) return;
            }
            break;
        case GATE_FAM_P40:
            if (n % 5 == 0 && !emit(cb, ctx, fam, 0, 0, n)) return;
            break;
        case GATE_FAM_M25:
            if (n % 4 == 0 && !emit(cb, ctx, fam, 0, 0, n)) return;
            break;
        case GATE_FAM_X15:
            if (n % 2 == 0 && !emit(cb, ctx, fam, 0, 0, n)) return;
            break;
        case GATE_FAM_SQRT:  // +√(s²) = s;根号下最多三位数
            for (int s = 1; s <= kmax && s <= 31; s++) if (!emit(cb, ctx, fam, 0, (uint16_t)s, n)) return;
            break;
        case GATE_FAM_INT:  // ∫₀ᵇ 2x dx = b²:值是完全平方数
            for (int b = 1; b * b <= kmax; b++) if (!emit(cb, ctx, fam, 0, (uint16_t)b, n)) return;
            break;
        case GATE_FAM_POW:  // 指数只能 2、3、4,底数从 2 起
            for (uint8_t e = 2; e <= 4; e++) {
                for (int b = 2;; b++) {
                    int k = 1;
                    for (int i = 0; i < e; i++) k *= b;
                    if (k > kmax) break;
                    if (!emit(cb, ctx, fam, e, (uint16_t)b, n)) return;
                }
            }
            break;
        case GATE_FAM_FACT:  // 3!、4!、5!、6!
            for (uint8_t f = 3; f <= 6; f++) {
                int k = 1;
                for (int i = 2; i <= f; i++) k *= i;
                if (k > kmax) break;
                if (!emit(cb, ctx, fam, f, 0, n)) return;
            }
            break;
        case GATE_FAM_DERIV:  // 值是 3a²
            for (int a = 1; 3 * a * a <= kmax; a++) if (!emit(cb, ctx, fam, 0, (uint16_t)a, n)) return;
            break;
        case GATE_FAM_SUM:  // 值是三角数
            for (int t = 1; t * (t + 1) / 2 <= kmax; t++) if (!emit(cb, ctx, fam, 0, (uint16_t)t, n)) return;
            break;
        default:
            break;
        }
    }
}

// ---- 候选值的位图(1–999 各一位,125 字节) ----

typedef struct {
    uint8_t bits[(GATE_SHEEP_MAX + 8) / 8];
} value_set_t;

static void set_add(value_set_t *s, int v) {
    s->bits[v >> 3] |= (uint8_t)(1u << (v & 7));
}

static bool set_has(const value_set_t *s, int v) {
    return v >= 1 && v <= GATE_SHEEP_MAX && (s->bits[v >> 3] >> (v & 7)) & 1u;
}

static bool collect_cb(void *ctx, const writing_t *w, int value) {
    (void)w;
    set_add((value_set_t *)ctx, value);
    return true;
}

// [lo, hi] 里有几个候选值。
static int set_count(const value_set_t *s, int lo, int hi) {
    int n = 0;
    for (int v = lo; v <= hi; v++) if (set_has(s, v)) n++;
    return n;
}

// [lo, hi] 里第 idx 个(从 0 数)候选值。
static int set_nth(const value_set_t *s, int lo, int hi, int idx) {
    for (int v = lo; v <= hi; v++) {
        if (set_has(s, v) && idx-- == 0) return v;
    }
    return 0;
}

// 从候选集里按约束挑 best > second > third。best 永远比 N 大(最好的一张不能反而让羊变少);
// need_gain 时 best 的增量还要 ≥ 0.6N。找到返回 true;所有 best 都配不出 second 和 third 时返回 false。
static bool pick_triple(gate_model_t *m, const value_set_t *s, int n, bool need_gain, int out[3]) {
    const int gain_min = need_gain ? ceil_pct(n, 60) : 1;
    const int best_lo = n + gain_min > 2 ? n + gain_min : 2;
    const int bests = set_count(s, best_lo, GATE_SHEEP_MAX);
    if (bests == 0) return false;
    const int start = (int)rand_below(m, (uint32_t)bests);
    for (int i = 0; i < bests; i++) {
        const int best = set_nth(s, best_lo, GATE_SHEEP_MAX, (start + i) % bests);
        const int second_lo = ceil_pct(best, gate_tuning.second_min_pct);
        const int third_hi = best * gate_tuning.third_max_pct / 100;
        const int seconds = set_count(s, second_lo, best - 1);
        const int thirds = set_count(s, 1, third_hi);
        if (seconds == 0 || thirds == 0) continue;
        out[0] = best;
        out[1] = set_nth(s, second_lo, best - 1, (int)rand_below(m, (uint32_t)seconds));
        out[2] = set_nth(s, 1, third_hi, (int)rand_below(m, (uint32_t)thirds));
        return true;
    }
    return false;
}

// ---- 给一个值挑写法 ----

typedef struct {
    int value;
    int count;       // 第一遍:数有几种写法;第二遍:还要跳过几个
    writing_t found;
    bool done;
} find_ctx_t;

static bool count_cb(void *ctx, const writing_t *w, int value) {
    find_ctx_t *f = (find_ctx_t *)ctx;
    (void)w;
    if (value == f->value) f->count++;
    return true;
}

static bool nth_cb(void *ctx, const writing_t *w, int value) {
    find_ctx_t *f = (find_ctx_t *)ctx;
    if (value != f->value) return true;
    if (f->count-- > 0) return true;
    f->found = *w;
    f->done = true;
    return false;
}

// 在 fams 里随机挑一种算出 value 的写法。没有时返回 false。
static bool find_writing(gate_model_t *m, uint32_t fams, bool allow_mul, int n, int value, writing_t *out) {
    find_ctx_t f = {.value = value};
    for_each_writing(fams, allow_mul, n, count_cb, &f);
    if (f.count == 0) return false;
    f.count = (int)rand_below(m, (uint32_t)f.count);
    for_each_writing(fams, allow_mul, n, nth_cb, &f);
    if (!f.done) return false;
    *out = f.found;
    return true;
}

static void fill_card(gate_card_t *c, const writing_t *w, int n, uint8_t rank) {
    writing_text(w, c->line1, c->line2);
    c->value = (uint16_t)clamp_sheep(writing_value(w, n));
    c->family = w->fam;
    c->rank = rank;
    c->mul = is_mul(w->fam);
}

// ④ 退化:三张加减卡,对任何 N 都满足约束。best 尽量 +kmax;N 太大加不上去时(N ≥ 800 左右,
// best 截成 999)second 和 third 用减法凑。值不能正好等于 N(写不出 ±0),等于时挪一位。
static void fallback_cards(gate_card_t *cards, int n) {
    const int kmax = add_max(n);
    const int best = clamp_sheep(n + kmax);
    const int second_lo = ceil_pct(best, gate_tuning.second_min_pct);
    int second = best - 1;
    if (second == n) second--;
    if (second < second_lo) second = second_lo;
    int third = best * gate_tuning.third_max_pct / 100;
    if (third == n) third--;
    if (third < 1) third = 1;
    const int values[3] = {best, second, third};
    for (int i = 0; i < 3; i++) {
        writing_t w = {.fam = GATE_FAM_ADD, .a = 0, .b = 1};  // best 截过顶时写 +1,截成 999
        if (values[i] > n) {
            w.b = (uint16_t)(values[i] - n);
        } else if (values[i] < n) {
            w.fam = GATE_FAM_SUB;
            w.b = (uint16_t)(n - values[i]);
        }
        fill_card(&cards[i], &w, n, (uint8_t)i);
    }
}

void gate_make_wave(gate_model_t *m) {
    const int n = m->sheep;
    const uint32_t fams = FAMILIES[m->level == GATE_HARD ? 1 : 0][(m->wave - 1) / 2];
    const bool allow_mul = m->mul_waves < gate_tuning.mul_waves_max;
    gate_card_t cards[3];
    bool ok = false;
    // ① 该波的家族,best 的增量 ≥ 0.6N;② 该波的家族,不限 best;③ 全部家族
    const struct { uint32_t fams; bool need_gain; } tries[3] = {
        {fams, true}, {fams, false}, {FAMS_ALL, false},
    };
    uint8_t stage = 3;
    for (int t = 0; t < 3 && !ok; t++) {
        value_set_t set;
        memset(&set, 0, sizeof(set));
        for_each_writing(tries[t].fams, allow_mul, n, collect_cb, &set);
        int values[3];
        if (!pick_triple(m, &set, n, tries[t].need_gain, values)) continue;
        ok = true;
        stage = (uint8_t)t;
        for (int i = 0; i < 3; i++) {
            writing_t w;
            if (!find_writing(m, tries[t].fams, allow_mul, n, values[i], &w)) {
                ok = false;  // 不该发生:值就是从这些写法里收集的
                break;
            }
            fill_card(&cards[i], &w, n, (uint8_t)i);
        }
    }
    if (!ok) fallback_cards(cards, n);  // ④
    if (m->wave >= 1 && m->wave <= GATE_WAVES) m->stage[m->wave - 1] = stage;

    // 三张卡随机分到三条跑道
    uint8_t lane_of[3] = {0, 1, 2};
    for (int i = 2; i > 0; i--) {
        const int j = (int)rand_below(m, (uint32_t)(i + 1));
        const uint8_t tmp = lane_of[i];
        lane_of[i] = lane_of[j];
        lane_of[j] = tmp;
    }
    bool mul = false;
    for (int i = 0; i < 3; i++) {
        m->cards[lane_of[i]] = cards[i];
        if (cards[i].mul) mul = true;
    }
    if (mul) m->mul_waves++;
}

void gate_init(gate_model_t *m, uint32_t seed, gate_level_t level) {
    memset(m, 0, sizeof(*m));
    m->state = GATE_READY_LEVEL;
    m->level = (uint8_t)level;
    m->lane = 1;
    m->countdown = GATE_COUNTDOWN_TICKS;
    m->sheep = gate_tuning.sheep_start;
    m->r_permille = 1000;
    m->rng = seed != 0 ? seed : SEED_FALLBACK;
}

static uint32_t move_lane(gate_model_t *m, int delta) {
    const int lane = (int)m->lane + delta;
    if (lane < 0 || lane >= GATE_LANES) return 0;
    m->lane = (uint8_t)lane;
    return GATE_EV_LANE;
}

uint32_t gate_input(gate_model_t *m, gate_input_t in) {
    const uint8_t st = m->state;
    if (st == GATE_FINISHED || st == GATE_ABORTED || st == GATE_WOLVES) return 0;
    switch (in) {
    case GATE_IN_UP:
    case GATE_IN_DOWN:
        if (st == GATE_READY_LEVEL) {
            const uint8_t level = in == GATE_IN_UP ? GATE_EASY : GATE_HARD;
            if (level == m->level) return 0;
            m->level = level;
            return GATE_EV_LEVEL;
        }
        if (st == GATE_PAUSED || st == GATE_APPLYING) return 0;
        return move_lane(m, in == GATE_IN_UP ? -1 : 1);
    case GATE_IN_OK_PRESS:
        if (st == GATE_WAVE || st == GATE_APPLYING) {
            m->paused_from = st;
            m->state = GATE_PAUSED;
            m->resume_on_release = false;  // 暂停那一下的松开不会继续
            return GATE_EV_PAUSED;
        }
        if (st == GATE_PAUSED) m->resume_on_release = true;
        return 0;
    case GATE_IN_OK_RELEASE:
        if (st == GATE_READY_LEVEL) {
            m->state = GATE_COUNTDOWN;
            m->countdown = GATE_COUNTDOWN_TICKS;
            return GATE_EV_COUNTDOWN;
        }
        if (st == GATE_PAUSED && m->resume_on_release) {
            m->state = m->paused_from;  // 暂停时卡片滑到一半的话,继续时接着滑
            m->resume_on_release = false;
            return GATE_EV_RESUMED;
        }
        return 0;
    case GATE_IN_OK_LONG:
        m->state = GATE_ABORTED;
        return GATE_EV_EXIT;
    }
    return 0;
}

uint16_t gate_next_permille(uint16_t r, uint16_t chosen, uint16_t best) {
    if (best == 0) return r;
    uint32_t next = (uint32_t)r * chosen / best;
    if (next < 1) next = 1;
    if (next > r) next = r;  // chosen 永远 ≤ best;防御一下
    return (uint16_t)next;
}

static void start_wave(gate_model_t *m) {
    m->wave++;
    m->wave_ticks = 0;
    m->wave_len = (uint16_t)(gate_tuning.wave_s[m->level == GATE_HARD ? 1 : 0][m->wave - 1] * TICKS_PER_S);
    m->apply_ticks = 0;
    m->state = GATE_WAVE;
    gate_make_wave(m);
}

// 选中的卡片滑到了:N 和 R 更新,然后下一波或者狼群。
static uint32_t apply_card(gate_model_t *m) {
    const gate_card_t *c = &m->cards[m->chosen_lane];
    uint16_t best = 0;
    for (int i = 0; i < GATE_LANES; i++) {
        if (m->cards[i].rank == 0) best = m->cards[i].value;
    }
    m->r_permille = gate_next_permille(m->r_permille, c->value, best);
    m->sheep = c->value;
    m->picks[m->wave - 1] = c->rank;
    uint32_t ev = c->rank == 0 ? GATE_EV_PICK_BEST : c->rank == 1 ? GATE_EV_PICK_SECOND : GATE_EV_PICK_THIRD;
    if (m->wave < GATE_WAVES) {
        start_wave(m);
        ev |= GATE_EV_WAVE;
    } else {
        m->state = GATE_WOLVES;
        m->wolves_ticks = 0;
        ev |= GATE_EV_WOLVES;
    }
    return ev;
}

uint32_t gate_tick(gate_model_t *m) {
    switch (m->state) {
    case GATE_COUNTDOWN:
        if (m->countdown > 0) m->countdown--;
        if (m->countdown > 0) return 0;
        start_wave(m);
        return GATE_EV_START | GATE_EV_WAVE;
    case GATE_WAVE:
        m->wave_ticks++;
        if (m->wave_ticks < m->wave_len) return 0;
        m->state = GATE_APPLYING;
        m->chosen_lane = m->lane;
        m->apply_ticks = 0;
        return GATE_EV_APPLYING;
    case GATE_APPLYING:
        m->apply_ticks++;
        if (m->apply_ticks < gate_tuning.apply_ticks) return 0;
        return apply_card(m);
    case GATE_WOLVES:
        m->wolves_ticks++;
        if (m->wolves_ticks < gate_tuning.wolves_ticks) return 0;
        m->state = GATE_FINISHED;
        return GATE_EV_FINISHED;
    default:  // READY_LEVEL、PAUSED、FINISHED、ABORTED
        return 0;
    }
}

const gate_card_t *gate_card(const gate_model_t *m, int lane) {
    return &m->cards[lane < 0 ? 0 : lane >= GATE_LANES ? GATE_LANES - 1 : lane];
}

unsigned gate_wave_ms_left(const gate_model_t *m) {
    const bool in_wave = m->state == GATE_WAVE || (m->state == GATE_PAUSED && m->paused_from == GATE_WAVE);
    if (!in_wave || m->wave_ticks >= m->wave_len) return 0;
    return (unsigned)(m->wave_len - m->wave_ticks) * GATE_TICK_MS;
}

unsigned gate_countdown_number(const gate_model_t *m) {
    if (m->state != GATE_COUNTDOWN) return 0;
    return (m->countdown + TICKS_PER_S - 1) / TICKS_PER_S;
}

uint32_t gate_wolves(const gate_model_t *m) {
    const uint32_t r = m->r_permille > 0 ? m->r_permille : 1;
    return ((uint32_t)m->sheep * gate_tuning.win_permille + r - 1) / r;
}

gate_result_t gate_result(const gate_model_t *m) {
    gate_result_t r;
    r.completed = m->state == GATE_FINISHED;
    r.won = r.completed && m->r_permille >= gate_tuning.win_permille;
    r.sheep = m->sheep;
    r.wolves = gate_wolves(m);
    r.r_permille = m->r_permille;
    return r;
}

bool gate_active(const gate_model_t *m) {
    return m->state == GATE_COUNTDOWN || m->state == GATE_WAVE || m->state == GATE_APPLYING ||
           m->state == GATE_WOLVES;
}

bool gate_in_round(const gate_model_t *m) {
    return m->state == GATE_READY_LEVEL || gate_active(m) || m->state == GATE_PAUSED;
}

int gate_text_cells(const char *s) {
    const uint8_t *p = (const uint8_t *)s;
    int cells = 0;
    while (*p) {
        uint32_t cp;
        int extra;
        if (p[0] < 0x80) {
            cp = p[0];
            extra = 0;
        } else if ((p[0] & 0xE0) == 0xC0) {
            cp = p[0] & 0x1Fu;
            extra = 1;
        } else if ((p[0] & 0xF0) == 0xE0) {
            cp = p[0] & 0x0Fu;
            extra = 2;
        } else {
            cp = p[0] & 0x07u;
            extra = 3;
        }
        for (int i = 1; i <= extra && p[i] != 0; i++) cp = (cp << 6) | (p[i] & 0x3Fu);
        p += 1 + extra;
        if (cp < 0x80) {
            cells += cp == '%' ? 2 : 1;
        } else if (cp == 0xB2 || cp == 0xB3 || cp == 0x2074 || cp == 0x2032) {
            cells += 1;  // ² ³ ⁴ ′
        } else {
            cells += 2;  // × ÷ √ ∫ ∑ → − 和中文
        }
    }
    return cells;
}

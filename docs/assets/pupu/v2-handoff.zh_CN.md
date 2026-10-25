<p align="right">
  <strong>简体中文</strong> · <a href="v2-handoff.md">English</a>
</p>

# 噗噗 v2 开发交接：摸摸、打工、羊过门

状态：方案定稿 · 2026-10-25 · 目标设备：FoloToy AI Passport（240 × 320 竖屏，↑ ↓ OK 三个实体键，无触摸，ESP32-C3，无 PSRAM）

> **一句话**：给噗噗加三样东西。摸摸有了手的动画和"摸多了会生气"；新功能"打工"赚零花钱、喂食要花钱、打工太多会 emo；小游戏「接星星」换成「羊过门」，三条跑道选算式卡片，有普通和学霸两档。微信小程序**不在**这次范围内，这次的所有功能不连手机也要能完整地玩。

这份文件给另一个开发线程用。它只有两部分：第 1 到 7 节是**定稿的产品规则**（数值都已拍板，不要再改），第 8 到 13 节是**实现清单**（文件、字段、测试、顺序、验收）。开发前先读仓库根目录的 `AGENTS.md`，用 `passport-develop` 技能；现有规则见 [`design.zh_CN.md`](design.zh_CN.md)，进度见 [`progress.zh_CN.md`](progress.zh_CN.md)。

---

## 0. 这次为什么这样设计

三个审稿代理对第一版草稿做过推演（一天看三次和一天看一次的玩家各算了一周的收支，小游戏模拟了 2000 局），发现三个问题，定稿时都改掉了：

1. **现有衰减太快，代价看不见。** 饱腹每小时 −10、开心每小时 −8，离开 8 小时后大多数情况拿起来都是 10 / 10（哪怕离开前是 100 / 100，也只剩 20 / 36）。那么打工扣 5 开心、第 5 次摸扣 5 开心，玩家几乎感知不到。所以衰减改慢到"睡一觉大约掉一个饭团"。
2. **emo 后打工若不封顶，可以无限刷钱。** 开心在 0 封底以后，每按一次都是纯收入。所以打工每天最多 5 次。
3. **狼群定在最优路径的 70% 听着宽松，实际是"6 波只能错一次"。** 乘法卡和加法卡混在一波，6 波复利以后差几十倍。所以卡片生成加了"最好和次好差不到 20%"的约束，门槛改成 60%，按"累计表现"而不是按绝对路径算。

另外两条原则：**亲密只增不减**保持不变（它是成长进度，倒退会让玩家觉得被罚），打工对亲密的代价用"占掉当天额度"实现；**星星糖仍然只从小游戏来**，它是解除 emo 的唯一快捷办法，所以才值钱。

---

## 1. 资源和衰减

噗噗身上现在有五个数。所有时间都是**宠物时间**（真实秒 × 倍率，见 `pupu_clock.h`）。

| 资源 | 范围 | 变化 | 和 v1 比 |
| --- | --- | --- | --- |
| 饱腹 | 0–100 | **每 15 分钟 −1（每小时 −4）**。离开最多补 8 小时 = −32，正好一个饭团 | v1 是每 6 分钟 −1 |
| 开心 | 0–100 | **每 20 分钟 −1（每小时 −3）；很饿（饱腹 < 15）时每 20 分钟 −2** | v1 是每 15 分钟 −2 / −4 |
| 亲密 | 只增不减，100 长大 | 每个宠物日的额度 20，所有来源共用；**打工会占掉额度**（见第 4 节） | 不变 |
| 星星糖 | 0–99 | 只有小游戏赢才给，每天最多 3 颗 | 不变 |
| **零花钱（新）** | **0–99** | **只有打工给。新孵化的噗噗有 30 元；旧存档迁移时也补成 30 元** | 新增 |

补算规则不变：数值最多补 8 小时，下限 min(10, 补算前的值)；天数按完整离开时长走。

**一天的账（一天看三次的玩家，供理解，不是规则）**：打工 2 次 +20 元；三个饭团 −15 元；剩 5 元买一块饼干。亲密额度 20 − 2 × 3 = 14，摸摸 9 + 小游戏赢 3 + 首次同步 2 正好用完，大约 7 天长大。完全不打工可以 5 天长大，但 30 元起步只够两天饭钱。

---

## 2. 喂食要花钱

| 食物 | 价格 | 效果 |
| --- | --- | --- |
| 饭团 | 5 元 | 饱腹 +35；饿时（饱腹 < 40）亲密 +3（不变） |
| 小饼干 | 3 元 | 饱腹 +10，开心 +5（60 分钟窗口内前 3 块加开心，第 4 块起"吃腻了"但照样扣钱）（效果不变） |
| 星星糖 | 免费，花 1 颗 | 开心 +25，亲密 +2（不变）；**emo 期间不受"饱腹 ≥ 90 不吃"的限制**，其他时候照旧 |

判断顺序（`pupu_model_feed()` 按这个顺序返回，前面命中就不再往下）：

1. 蛋阶段 → `PUPU_NOT_HATCHED`。
2. 星星糖且库存为 0 → `PUPU_NO_CANDY`。
3. **emo 中喂小饼干 → `PUPU_SULKING`（新）**：摇头，气泡"没胃口"，不扣钱、不计数、不开窗口。
4. 饱腹 ≥ 90，且不是"emo 中的星星糖" → `PUPU_REFUSED`：摇头"吃不下啦"，不扣钱。
5. **饭团或饼干而 `coin` 小于价格 → `PUPU_NO_COIN`（新）**：摇头，气泡"没钱啦，去打工吧"，什么都不变。
6. 生效并扣钱。星星糖免费，不查钱；喂星星糖时 `emo_min = 0`。

价格按份算：饱腹 89 时喂饭团只加到 100，仍扣 5 元；饼干"吃腻了"照扣 3 元。

喂食页顶部把"星星糖 ×2"改成"**零花钱 15  糖 ×2**"（两个短标签，右对齐）。选中买不起的食物时，底部胶囊里的字用淡色（`PUPU_COLOR_MUTED`）。

---

## 3. 摸摸（原"摸摸头"，改名）

菜单项、气泡和文档里的"摸摸头"全部改成"**摸摸**"。10 分钟窗口不变：第 1 次开窗，窗口到期后计数清零。窗口内按次数：

| 第几次 | 开心 | 亲密 | 噗噗 | 画面和声音 |
| --- | --- | --- | --- | --- |
| 1、2、3 | +5 | +1 | 开心跳一下 | 小手从宠物上方落到头上拍一下再抬起，"啪"；气泡"开心 +5  亲密 +1" |
| 4 | 0 | 0 | **不耐烦**（新表情：皱眉侧头），**一直保持到窗口结束** | 小手落下，气泡"够了啦"，"哼" |
| 5 | **−5** | 0 | **激烈反抗**（新表情：生气跳开） | 小手落下被弹开（往右上飞走），气泡"别摸啦！  开心 −5"，三连降音 |
| 6 起 | 0 | 0 | 转身（复用躲开姿势） | **不出小手、不出气泡** |

计数规则（和 v1 不同）：**窗口内每次按下都计数，含第 4 次以后**，计到 5 以后饱和（第 6 次起不再加、不延长窗口）。emo 期间摸摸直接躲开，气泡"不想理你"，不计数、不开窗口。亲密仍走每日 20 额度。

---

## 4. 打工（新）

放在玩耍页，菜单变成三项：**摸摸 / 打工 / 羊过门**。按每个宠物日计数，过了宠物日清零（和 `candy_today` 一样在 `advance_day()` 里清）。

| 当天第几次 | 零花钱 | 开心 | 当天亲密额度 | 噗噗 | 画面和声音 |
| --- | --- | --- | --- | --- | --- |
| 1、2 | +10 | −5 | **−3**（`bond_today += 3`，亲密本身不变） | 搬表格（新表情：抱着一张表、冒汗） | Excel 表格从屏幕上方掉下来，噗噗接住，"叮叮"（收钱）；气泡"零花钱 +10  开心 −5  今天亲密额度 −3" |
| 3 | +10 | −5 | −3 | 被砸晕（新表情：倒地、头顶转圈） | 表格砸中头，"砰"；然后进入 emo，气泡"累坏了…要休息 4 小时" |
| 4、5（emo 中） | **+3** | **−10** | 0 | 被砸晕 | "砰"，气泡"做错表了…  零花钱 +3  开心 −10" |
| 6 起 | 0 | 0 | 0 | 摇头 | 气泡"做不动了"，"NO"音效 |

第几次只看打工前的 `work_today`（先 +1 再比较），**不看 `emo_min`**：星星糖提前解除了 emo，当天第 4、5 次仍是 +3 / −10；跨宠物日后 `work_today` 清零，哪怕 emo 还没结束，新一天的第 1、2 次仍是 +10 / −5。第 3 次无条件把 `emo_min` 设成 240（已经在 emo 中也重置）。

额度规则：`bond_today` 最多加到 20，气泡显示实际占掉的数（18 → 20 时写 −2，已满时 0，但照常给钱）。零花钱加到 99 封顶（95 元时打工只记 +4，仍算一次打工、仍扣开心、仍占额度）。蛋阶段不能打工（`PUPU_NOT_HATCHED`）。

---

## 5. emo

- **进入**：当天第 3 次打工结束时。`emo_min = 240`（4 小时宠物时间）。
- **倒计时**：在 `step_minute()` 里每分钟 −1，和 `cookie_win_min` 一样；补算最多走 8 小时，所以深睡或关机超过 4 小时醒来必然已解除，跨天、跨深睡都不用特判。
- **表情**：`emo_min > 0` 时待机动画换成 emo（垂头、头顶一朵小乌云），首页、喂食页、玩耍页都显示。空闲姿势的优先级：**很饿趴着 > emo > 不耐烦（第 3 节）> 普通待机**；气泡优先级同理（"饿了" > "累坏了"）。
- **期间**：摸摸只躲开（"不想理你"）；小饼干拒绝（摇头"没胃口"，不扣钱）；饭团照吃；打工见第 4 节第 4、5 行；小游戏照常玩、赢了照常给奖励，但**不解除 emo**。
- **提前解除**：只有喂一颗星星糖（立刻 `emo_min = 0`，并照常开心 +25、亲密 +2）。
- 气泡里的小时数按 `(emo_min + 59) / 60` 显示。

---

## 6. 小游戏「羊过门」（替换「接星星」）

**一局**：开局选难度（↑↓ 选"普通 / 学霸"，OK 开始）→ 3-2-1 倒数 → 6 波 → 狼群结算 → 结果牌。↑↓ 换跑道、OK 暂停、长按 OK 退出、暂停时熄屏作废、中途退出不算一局，全部沿用接星星。

**画面**：沿用 `catch_layout.h` 的三条 80 px 跑道，噗噗在左边 x 8–56。每一波是三张**静止**的卡片，一条跑道一张，放在 x 140–220、高 64 px、圆角、纸色底墨色边；卡片不飞。顶栏左边"羊 N"，右边"第 k/6 波"，顶栏下面一条倒计时条从满到空。时间到，噗噗所在跑道那张卡片用 0.3 秒滑到噗噗身边生效，另外两张消失；然后下一波出现。第 6 波之后狼群图标带着数字 W 从右边滑进来，和 N 比大小，出结果牌。

**数值**：
- 羊群 N 起始 10，范围 1–999（结果向下取整，低于 1 按 1，高于 999 按 999）。
- 每一波先按当前 N 定三个**结果值** best > second > third，用交叉相乘判断：`second × 100 >= best × 80`，`third × 100 <= best × 60`，三个值互不相同、都在 1–999 内。再给每个值挑一种**写法**（见卡池）。这样"数字看起来最大"的卡不一定最好，必须真算。
- 表现比 `R`（千分比，`uint16_t`，起始 1000）：每波 `R = R × chosen / best`（用 `uint32_t` 中间值做整数运算，结果低于 1 按 1）。**赢的条件：6 波后 `R >= 600`**。选次好一次（×0.8）赢，两次（0.64）赢，三次（0.51）输；选到最差的一张（≤ 0.6）之后再错任何一次就输。
- 狼群数量只用来显示：`W = (N × 600 + R − 1) / R`（向上取整，`uint32_t`），所以"N ≥ W"和"R ≥ 600"永远一致；顶栏和结果牌显示 W 时封到 9999。
- 每波卡片生效后立刻给反馈：选到 best 气泡"最佳！"（绿边闪一下），second"还行"，third"差了"；配两个短音效。
- 随机数用带种子的 xorshift32（沿用 `catch_model.c`），同一个种子、同样的按键必定得到同一局。

**卡池（写法）**。每张卡最多两行，宽度规则见表后的"格"。运算从左到右依次做，没有括号。

| 写法家族 | 例子 | 值 | 只在什么时候出 |
| --- | --- | --- | --- |
| 加减 | `+36`、`−9` | 字面值 | 加法常数取 0.6N–1.2N，减法常数取 0.2N–0.5N |
| 乘除 | `×2`、`×3`、`÷2`、`÷3` | 字面值 | ÷2 只在 N 为偶数，÷3 只在 N 能被 3 整除 |
| 两步 | `×2 −15`、`+9 ×2`、`+40%`、`−25%`、`×1.5` | 依次算 | +40% 只在 N 是 5 的倍数，−25% 只在 4 的倍数，×1.5 只在偶数 |
| 平方根 | `+√144` | 12 | 值是完全平方数 |
| 幂 | `+6²`、`+3³`、`+2⁴` | 36、27、16 | 指数只能 2、3、4（字库没有 ⁵ 以上） |
| 阶乘 | `+3!`、`+4!` | 6、24 | |
| 积分 | 第一行 `+∫2x dx`，第二行 `0→6` | 36（= b²） | 值是完全平方数；字库没有下标，上下限写在第二行 |
| 导数 | 第一行 `+(x³)′`，第二行 `x=3` | 27（= 3a²） | 值是 3a² |
| 求和 | 第一行 `+∑k`，第二行 `k=1→8` | 36（= n(n+1)/2） | 值是三角数 |

生成办法是**先列候选，再选三张**：候选集 = 该波允许的家族在当前 N 下能写出的全部（写法, 值）。所有加法类家族（加减、平方根、幂、阶乘、积分、导数、求和）的加数 k 取 `1 ≤ k ≤ max(⌊1.2N⌋, 6)`，减数 k 取 `⌈0.2N⌉ ≤ k ≤ ⌊0.5N⌋`（N < 5 不出减法），乘除按表里的整除条件。按下面的顺序找满足约束的三元组，找到就停：① 该波的家族，且 best 的加数 ≥ 0.6N；② 该波的家族，不限 best；③ 全部家族；④ 退化成三张 `+kmax`、`+⌈0.8 × kmax⌉`、`+1`（kmax = max(⌊1.2N⌋, 6)），④ 对任何 N ≥ 1 都满足约束。

卡片文字宽度按"格"算，规则层提供 `gate_text_cells()`：ASCII 1 格，`%` 2 格，`× ÷ √ ∫ ∑ → −` 2 格，`² ³ ⁴ ′` 1 格，中文 2 格；**每行最多 9 格**（9 × 8 px = 72 px，卡宽 80 减左右各 4 px）。卡片里的减号统一用 `−`（U+2212）。

**档位**：

| 档位 | 第 1–2 波 | 第 3–4 波 | 第 5–6 波 | 每波秒数 |
| --- | --- | --- | --- | --- |
| 普通 | 加减乘除 | 两步 | 平方根、幂、阶乘 | 4、4、6、6、6、6 |
| 学霸 | 两步 | 积分、导数 | 求和、幂、×1.5 | 6、6、8、8、8、8 |

学霸一局最长 44 秒 + 倒数 3 秒 + 结算约 3 秒。乘法卡（×2、×3、×1.5、+40%）每局最多出现在 2 波里，所以 N 很难碰到 999。

**奖励**（不变）：完成一局饱腹 −5；赢：开心 +15、亲密 +3、星星糖 +1（每天最多 3 颗，库存 99 封顶）。结果牌沿用接星星的格式。

---

## 7. 不变的规则

同步协议（线上字节不变，`proto_ver` 仍是 1）、每日首次同步亲密 +2、蛋孵化、长大、熄屏、深睡、存档时机、按键过滤都不动。首页四个入口不变。`design.zh_CN.md` 第 12 节的细则除本文改动的以外继续有效。

---

## 8. 实现清单：规则核心（纯逻辑，先做，先测）

### 8.1 `main/pupu_config.h`

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `PUPU_FULL_DECAY_EVERY_MIN` | 6 → **15** | |
| `PUPU_HAPPY_DECAY_EVERY_MIN` | 15 → **20** | |
| `PUPU_HAPPY_DECAY` | 2 → **1** | |
| `PUPU_HAPPY_DECAY_STARVING` | 4 → **2** | |
| `PUPU_COIN_MAX` | 99 | 新 |
| `PUPU_COIN_START` | 30 | 新：新孵化和 v1 迁移 |
| `PUPU_RICE_PRICE` / `PUPU_COOKIE_PRICE` | 5 / 3 | 新 |
| `PUPU_PAT_HAPPY` / `PUPU_PAT_BOND` | 5 / 1 | 不变，只对第 1–3 次 |
| `PUPU_PAT_LIMIT` | 3 | 不变：第 4 次起不加 |
| `PUPU_PAT_ANGRY_AT` | 5 | 新：这一次开心 −`PUPU_PAT_ANGRY_HAPPY`（5） |
| `PUPU_PAT_COUNT_MAX` | 5 | 新：计数饱和值 |
| `PUPU_WORK_COIN` / `PUPU_WORK_HAPPY_COST` / `PUPU_WORK_BOND_BUDGET` | 10 / 5 / 3 | 新 |
| `PUPU_WORK_EMO_AT` | 3 | 新：第 3 次结束后进入 emo |
| `PUPU_WORK_EMO_COIN` / `PUPU_WORK_EMO_HAPPY_COST` | 3 / 10 | 新 |
| `PUPU_WORK_PER_DAY` | 5 | 新：第 6 次起拒绝 |
| `PUPU_EMO_MIN` | 240 | 新：宠物分钟 |

小游戏结算常量：删掉 `PUPU_CATCH_WIN_STARS`（8），把 `PUPU_CATCH_FULL_COST` / `PUPU_CATCH_HAPPY` / `PUPU_CATCH_BOND` 改名为 `PUPU_GATE_FULL_COST`（5）、`PUPU_GATE_HAPPY`（15）、`PUPU_GATE_BOND`（3）；`PUPU_CANDY_PER_DAY` 不变。**`PUPU_CATCH_UP_MAX_S`、`PUPU_CATCH_UP_FLOOR` 是离开补算（catch-up）的常量，不要动。** 这些删除和改名在 M7c 做（见第 10 节）。

### 8.2 `main/pupu_model.h` / `.c`

- `pupu_model_t` 新字段：`uint8_t coin;`、`uint8_t work_today;`、`uint16_t emo_min;`。放在结构体末尾。
- `pupu_result_t` 新值：`PUPU_NO_COIN`（钱不够）、`PUPU_ANNOYED`（第 4 次摸）、`PUPU_ANGRY`（第 5 次摸）、`PUPU_SULKING`（emo 时摸摸或饼干）、`PUPU_WENT_EMO`（第 3 次打工，成功且进入 emo）、`PUPU_OVERWORKED`（第 4、5 次打工，成功，+3 / −10）、`PUPU_EXHAUSTED`（第 6 次起，什么都不变）。
- `pupu_outcome_t` 新字段：`int8_t coin;`（−5…+10）、`int8_t bond_budget;`（打工时 −3，给气泡用）、`bool emo_cleared;`（喂星星糖解除了 emo）。
- `pupu_model_init()`：`coin = PUPU_COIN_START`。
- `step_minute()`：`if (m->emo_min > 0) m->emo_min--;` 放在窗口递减旁边，蛋阶段也走（蛋不会 emo，无影响）。
- `advance_day()`：清 `work_today`。
- `pupu_model_feed()`：按第 2 节的六步顺序重写；喂星星糖时 `emo_min = 0` 并置 `emo_cleared`。
- `pupu_model_pat()`：按第 3 节重写计数；emo 时返回 `PUPU_SULKING`。
- 新函数 `pupu_outcome_t pupu_model_work(pupu_model_t *m);`，按第 4 节：第 1、2 次返回 `PUPU_OK`，第 3 次 `PUPU_WENT_EMO`，第 4、5 次 `PUPU_OVERWORKED`，第 6 次起 `PUPU_EXHAUSTED`。
- 新增 `pupu_model_apply_gate_result(pupu_model_t *m, bool completed, bool won);`，逻辑同 `pupu_model_apply_catch_result()`（赢 = `won`）。旧函数先保留，M7c 替换小游戏时再删（见第 10 节）。
- 头文件注释里把"摸摸头"改成"摸摸"，并补充 emo 和零花钱的说明。
- `tests/test_pupu_model.c` 至少加这些用例：摸摸 1–6 次每一档（含第 4 次常驻、第 5 次 −5、第 6 次不计数、窗口到期清零）；emo 中摸摸 → `PUPU_SULKING` 且状态不变；打工 1–6 次每一档、第 3 次 `emo_min == 240`、已在 emo 中第 3 次重置；`emo_min` 随 `advance` 和 `catch_up` 递减到 0；跨宠物日清 `work_today` 不清 `emo_min`；emo + 饱腹 95 + 饼干 → `PUPU_SULKING`；emo + 0 元 + 饼干 → `PUPU_SULKING`；emo + 饱腹 95 + 星星糖 → `PUPU_OK`、`emo_cleared`、`emo_min == 0`；饭团没钱 → `PUPU_NO_COIN`；饱腹 ≥ 90 时饭团先 `PUPU_REFUSED` 不扣钱；新衰减的每小时数值；打工占额度到 20 封顶。现有 `test_pat_window`（第 273 行附近）的"躲开不计数"预期要改。

### 8.3 存档 `main/pupu_save_codec.h` / `.c`

- `PUPU_SAVE_VERSION` 1 → 2。**把长度常量拆开**：`PUPU_SAVE_PAYLOAD_SIZE_V1 = 51`、`PUPU_SAVE_PAYLOAD_SIZE_V2 = 55`（v1 的 51 字节后面接 `coin` u8、`work_today` u8、`emo_min` u16），`PUPU_SAVE_PAYLOAD_SIZE` 指向 V2，记录总长 71 字节。
- `decode_payload()`：`case 1` 用 V1 常量，调用 `decode_v1()` 后补 `coin = PUPU_COIN_START`、`work_today = 0`、`emo_min = 0`；新增 `case 2`。现在的写法如果只改一个常量，**所有旧存档会被判成 BAD_LENGTH，玩家的噗噗会被清掉**，这是必须避免的。
- `tests/test_pupu_save_codec.c`：保留一份 v1 逐字节样例证明旧档能读并得到 30 元；v2 往返；错误长度。
- `tests/pupu_sync_vectors.txt` 第 39–51 行是一份 67 字节的 v1 存档样例（`save_info` 的 `save_ver=1`、`total_len=67`、`chunk_count=4`）。要改的不止这一段：
  - 存档变成 71 字节后分成 5 块（17 + 17 + 17 + 17 + 3），要多一对 `save_get_4` / `save_chunk_4`（共 18 对），`save_info` 变成 `save_ver=2 total_len=71 chunk_count=5`，`save_get_oob` 的 idx 改成 5，新 payload 末尾 4 字节是 `1e 00 00 00`（coin = 30、work_today = 0、emo_min = 0）。
  - 衰减改了以后，`status_synced` / `status_retry` / `status_earlier` 三行和 `save_chunk_0` 里的饱腹、开心字节也变（文件头注释"catch up 3 h (full −30, happy −24)"按新衰减是 −12 / −9）。
  - `tests/test_pupu_sync_proto.c` 里写死的 `pairs == 17`、`chunk_count(...) == 4`、`PUPU_SAVE_RECORD_SIZE == 67`、最后一块 `3 + 16` 要改成 18 / 5 / 71 / 3；`tests/test_pupu_sync_web.mjs` 里的 17 req / 17 resp 同样改。
  - 仓库里没有生成向量的工具。建议给 `test_pupu_sync_proto.c` 加一个 `--dump` 参数，按向量文件的顺序把每条回复打成十六进制，用它覆盖向量文件，再跑 `test_pupu_sync_web.mjs` 交叉验证。

  协议本身不变（`proto_ver` 仍是 1），网页按 `total_len` 动态拼块，不用改。

### 8.4 小游戏规则 `main/gate_model.h` / `.c`、`main/gate_layout.h`

替换 `catch_model.*`、`catch_layout.h`（删除旧文件；跑道布局常量搬到 `gate_layout.h`，`CATCH_` 前缀改 `GATE_`）。接口建议：

```c
typedef enum { GATE_EASY = 0, GATE_HARD } gate_level_t;
void gate_init(gate_model_t *m, uint32_t seed, gate_level_t level);
uint32_t gate_input(gate_model_t *m, gate_input_t in);   // UP/DOWN/OK_PRESS/OK_RELEASE/OK_LONG，同 catch
uint32_t gate_tick(gate_model_t *m);                      // 20 ms 一步
const gate_card_t *gate_card(const gate_model_t *m, int lane);  // 当前波的卡片：两行文字 + 值
unsigned gate_wave_ms_left(const gate_model_t *m);        // 倒计时条
int gate_text_cells(const char *utf8);                    // 第 6 节的格数规则，测试和 UI 共用
gate_result_t gate_result(const gate_model_t *m);         // { bool completed; bool won; uint16_t sheep; uint32_t wolves; uint16_t r_permille; }
```

事件位沿用 `CATCH_EV_*` 的做法，加 `GATE_EV_WAVE`（新一波出现）、`GATE_EV_PICK_BEST/SECOND/THIRD`、`GATE_EV_WOLVES`。状态机：`READY_LEVEL`（选难度）→ `COUNTDOWN` → `WAVE` → `APPLYING`（0.3 秒滑动）→ … → `WOLVES` → `FINISHED` / `ABORTED`。`READY_LEVEL` 里 ↑↓ 切换档位，**OK 松开**（`GATE_IN_OK_RELEASE`）才开始倒数，所以按住 0.5 秒的 `GATE_IN_OK_LONG` 直接退出、不会先开一局；上次选的档位记在 `pupu_ui_gate.c` 的 static 变量里，下次进页面默认选它。`gate_in_round()` 在 `READY_LEVEL` 时为 true（按键走 GAME 模式、不定时存档），`gate_active()` 在 `READY_LEVEL` 时为 false（和暂停一样可以变暗、熄屏；熄屏时作废、亮屏回玩耍页），其余状态和接星星一致。卡片文字由规则层生成为两行 `char[16]`（UTF-8），UI 只负责显示，这样文字宽度能在主机测试里检查。

`tests/test_gate_model.c`（替换 `test_catch_model.c`，并改 `tools/validate.sh` 第 63–65 行的编译和运行行）要覆盖：N 从 1 到 999、两档、每个波位下生成的三张卡都满足交叉相乘约束、互不相同、落在 1–999；所有写法的值都是整数且和显示的一致；每行 `gate_text_cells()` ≤ 9；卡片里每个非 ASCII 字符都在 `PUPU_TXT_GATE_GLYPHS`（9.4 节）里；÷、%、×1.5 的整除条件；`R` 的更新用固定比值单元测试（1000 → 800 → 640 赢，→ 512 输），不对随机卡片断言；固定种子下连续 6 波都选最差（含 third = 1、best = 999 的极端情况）不崩、`R >= 1`、`W > N`；乘法卡每局 ≤ 2 波；同一种子可复现；暂停、退出、作废语义和接星星一致。

---

## 9. 实现清单：界面、美术、声音、字库

### 9.1 页面

- `main/pupu_ui.h`：`pupu_pose_t` 加 `PUPU_POSE_ANNOYED`、`PUPU_POSE_ANGRY`、`PUPU_POSE_WORK`、`PUPU_POSE_DIZZY`、`PUPU_POSE_EMO`，末尾加 `PUPU_POSE_COUNT`；`pupu_ui_pet_anim()` 的两张表各加五项，两条 `_Static_assert` 里的 `PUPU_POSE_WAVE + 1` 改成 `PUPU_POSE_COUNT`（不改断言会编不过）。空闲姿势只有一个来源：新函数 `pupu_ui_idle_pose(m)`（放 `pupu_ui.c`，首页和照顾页共用），按 很饿 > emo > 不耐烦 > 普通 返回 `IDLE` / `EMO` / `ANNOYED`；`pupu_ui_pet_anim()` 对 `IDLE`、`EMO`、`ANNOYED` 三种姿势都做"很饿趴着"替换，反应姿势（`EAT` / `HAPPY` / `DODGE` / `SHAKE` / `WORK` / `DIZZY` / `ANGRY`）不替换。`PUPU_UI_GO_CATCH` 改 `PUPU_UI_GO_GATE`（M7c）。
- `main/pupu_ui_care.c`：玩耍页三项 `摸摸 / 打工 / 羊过门`；`pat()` 按结果分支显示五种反应；新增 `work()`；喂食页顶部显示零花钱；买不起时胶囊文字淡色；小手和表格用 `lv_image` 叠在宠物画布上方，照顾页的主循环是 50 ms（`LOOP_MS`），每圈挪一次位置（落下 0.3 秒 = 6 步 × 10 px），反应结束时藏起。不耐烦的条件是 `pat_win_min > 0 && pat_count >= 4`，由 `pupu_ui_idle_pose()` 判断。
- `main/pupu_ui_home.c`：只要 `pupu_ui_pet_anim()` 处理了 emo 待机就不用改；气泡优先级"饿了" > "累坏了"。
- `main/pupu_ui_status.c`：加一行"零花钱"，`ROW_STEP` 56 → 44，五行 y = 60 / 104 / 148 / 192 / 236，最后一条进度条到 270，在底部"长按 OK 返回"提示（约 y 287 起）之上。
- `main/pupu_ui_gate.c`（替换 `pupu_ui_catch.c`）：难度选择 → 倒数 → 波 → 结算。LVGL 对象预算：3 张卡（矩形 + 2 个标签）、倒计时条、顶栏 2 个标签、狼群图标 + 标签、结果牌，全部进页面时建好，之后只藏显和挪位置。保留现有的帧率 / 换道延迟 / LVGL 内存日志行用于真机验收。
- `main/pupu_app.c`：`PAGE_CATCH` → `PAGE_GATE`，相关调用改名；其余（20 ms 主循环、存档延后、暂停熄屏作废）沿用。

### 9.2 像素图（先用脚本画临时版）

`tools/make_placeholder_art.py` 的 `ANIMS` 末尾追加（幼年、成年各一套，每套 2 帧，追加在最后以免改动已有帧的字节）：`child_annoyed`、`child_angry`、`child_work`、`child_dizzy`、`child_emo` 和对应的 `adult_*`。emo 的小乌云用 15 号色（`PUPU_SLOT_CLOUD`）；表情帧只能用调色板里的 16 色。

`ICONS` 加 `icon_hand`、`icon_sheet`（Excel 表格：绿底白格子）、`icon_wolf`，建议 **48 × 48**（给 `gen_pupu_assets.py` 的 `ICONS` 加尺寸字段，`ICON_W` 不再是唯一尺寸），每个 6,912 字节。注意现在 `gen_pupu_assets.py` 的 `read_strip()` 对图标也只接受 `pupu.gpl` 的 16 色，而调色板里没有绿色：把图标改成直接读 RGBA、不查调色板（它们本来就输出 RGB565A8），`make_placeholder_art.py` 画图标时直接用 RGB 颜色。`icon_star`、`icon_cloud` 在 M7c 删除；删掉星星后 14 号色空出来，15 号乌云灰留给 emo 的小乌云。

然后 `/usr/bin/python3 tools/make_placeholder_art.py && /usr/bin/python3 tools/gen_pupu_assets.py`，`tests/test_pupu_assets_fresh.py` 会检查哈希；更新 `assets/images/pupu/README.md` / `.zh_CN.md` 的文件表。新增约 23 KB 帧数据 + 21 KB 图标，Flash 余量 7 MB，没有压力。

### 9.3 音效 `main/pupu_sfx.h` / `.c`

新增：`PUPU_SFX_PAT`（啪）、`PUPU_SFX_ANNOYED`（哼）、`PUPU_SFX_ANGRY`（三连降）、`PUPU_SFX_COIN`（叮叮）、`PUPU_SFX_THUD`（砰：噪声 40 ms + 110 Hz 方波 160 ms，比 CLOUD 更低更长）、`PUPU_SFX_SIGH`（叹气：下滑长音，在 `PUPU_SULKING` 时放，即 emo 中摸摸或饼干"没胃口"）、`PUPU_SFX_CARD_GOOD`、`PUPU_SFX_CARD_BAD`、`PUPU_SFX_HOWL`（狼嚎）。`PUPU_SFX_STAR`、`PUPU_SFX_CLOUD` 在 M7c 删除。WIN / LOSE / BEEP / NO 复用。

### 9.4 文字和字库

`main/pupu_text.h` 新增（以 `tests/test_pupu_charset.py` 报出的缺字为准）：

| 宏 | 文字 |
| --- | --- |
| `PUPU_TXT_PAT` | 摸摸头 → **摸摸** |
| `PUPU_TXT_WORK` / `PUPU_TXT_GATE` | 打工 / 羊过门 |
| `PUPU_TXT_COIN` / `PUPU_TXT_COIN_COUNT` | 零花钱 / 零花钱 %d |
| `PUPU_TXT_CANDY_SHORT` | 糖 ×%d |
| `PUPU_TXT_NO_COIN` | 没钱啦，去打工吧 |
| `PUPU_TXT_ANNOYED` / `PUPU_TXT_ANGRY` | 够了啦 / 别摸啦！ |
| `PUPU_TXT_SULKING` / `PUPU_TXT_NO_APPETITE` | 不想理你 / 没胃口 |
| `PUPU_TXT_WENT_EMO` | 累坏了…要休息 %u 小时 |
| `PUPU_TXT_EMO_WORK` / `PUPU_TXT_EXHAUSTED` | 做错表了… / 做不动了 |
| `PUPU_TXT_BOND_BUDGET` | 今天亲密额度 |
| `PUPU_TXT_LEVEL_EASY` / `PUPU_TXT_LEVEL_HARD` / `PUPU_TXT_PICK_LEVEL` | 普通 / 学霸 / ↑ ↓ 选难度，OK 开始 |
| `PUPU_TXT_SHEEP` / `PUPU_TXT_WAVE` / `PUPU_TXT_WOLVES` | 羊 %u / 第 %u/6 波 / 狼群 ×%u 来了！ |
| `PUPU_TXT_PICK_BEST` / `_SECOND` / `_THIRD` | 最佳！ / 还行 / 差了 |
| `PUPU_TXT_GATE_WIN` / `PUPU_TXT_GATE_LOSE` | 羊比狼多，赢啦！ / 羊不够多… |
| `PUPU_TXT_GATE_GLYPHS` | `+−×÷√∫∑′²³⁴→%!()xdk=`：卡片用到的全部非中文字符，放进 `PUPU_TEXT_LIST`，`gate_model.c` 只能用这些符号，这样 `test_pupu_charset.py` 和 `pupu_fonts_selfcheck()` 都能查到。它本身不上屏 |

`assets/fonts/pupu/charset.txt` 要加的符号：`÷ √ ∫ ∑ ′ ² ³ ⁴ → −`（最后一个是减号 U+2212，卡片里的减号统一用它；源字体 Noto Sans SC 都有；**没有** ⁵–⁹、下标 ₀–₉、∛，不要用）。中文新字约 36 个，由 `tests/test_pupu_charset.py` 报出后一次加齐。重新生成命令在 `assets/fonts/pupu/README.zh_CN.md`，需要 Node 22（本机已是 v22.22.1），不要用 Node 24。新增约 46 个字形、约 6 KB。

---

## 10. 实现顺序和每步的验收

| 子里程碑 | 做什么 | 怎么算完成 |
| --- | --- | --- |
| **M7a 规则核心** | 8.1–8.3：config、model、存档 v2、同步向量重生成 | `./tools/validate.sh --static` 全绿；`test_pupu_model` 覆盖第 1–5 节每一行数字；v1 存档样例解码得 30 元 |
| **M7b 摸摸、打工、emo** | 9.1 的 care / home / status 页，9.2 的五个表情和两个图标，9.3 的六个音效，9.4 的字库 | `--static` 全绿；`--firmware` 编译通过且没有警告；`pupu_fonts_selfcheck()` 开机不报缺字 |
| **M7c 羊过门** | 8.4、`pupu_ui_gate.c`、狼图标、卡片音效、文字；**所有接星星的删除和改名都在这一步**（8.1 的四个常量、8.2 的旧函数、9.2 的两个图标、9.3 的两个音效、`PUPU_UI_GO_CATCH` / `PAGE_CATCH`、`test_catch_model.c` 和 `validate.sh` 那三行） | `test_gate_model` 全绿；`--firmware` 通过；串口日志每秒最少帧 ≥ 30、一帧最长 ≤ 40 ms、换道到画出 ≤ 50 ms、LVGL 池剩余 ≥ 4 KB（接星星 M5 的验收线；M5 实测 36–40 帧、换道 33–51 ms、池剩余约 37 KB，供对照） |
| **M7d 文档和完整检查** | 第 11 节的文档；`./tools/validate.sh` 完整跑 | 完整门禁通过；进度文档记编译身份 |

**M7a、M7b 只新增、不删除、不改名**：`pupu_model_apply_catch_result()`、`PUPU_CATCH_WIN_STARS` 等四个常量、`PUPU_SFX_STAR` / `CLOUD`、`icon_star` / `icon_cloud`、`PUPU_UI_GO_CATCH`、`PAGE_CATCH` 都保留，新增的 `pupu_model_apply_gate_result()` 和 `PUPU_GATE_*` 与它们并存，这样 M7a、M7b 结束时固件都能编译、接星星照常能玩。

每个子里程碑结束时按 `AGENTS.md` 的格式单独报告 Build / Host tests / Device tests / Unverified，并询问是否刷机。四个子里程碑可以各自提交到 `feature/pupu-v1`（要先征得同意），提交信息按 `docs/contribution/commit-and-pr.md`。

---

## 11. 要改的文档

- `docs/assets/pupu/design.md` / `.zh_CN.md`：第 2 节（五个数、新衰减）、第 4 节（价格）、第 5 节（摸摸、打工、羊过门）、第 7 节（按键表加难度选择）、第 9 节（"第一版不做的事"去掉"金币和商店"）、第 12 节（第 2、6、10、11、12、13、14 条按本文改）。标题版本改成 v2，日期 2026-10-25。
- `docs/assets/pupu/progress.md` / `.zh_CN.md`：里程碑表插入 **M7 噗噗 v2（摸摸、打工、羊过门）**，原 M7 微信小程序改为 **M8**，原 M8 整机验收改为 **M9**；"当前状态"和"下一步"同步改；加 M7a–M7d 的记录。
- `assets/images/pupu/README.md` / `.zh_CN.md`：文件表加新动画和图标，删星星、乌云。
- 本文件：完成后在顶部加"已实现于提交 xxxx"一行即可，不删。
- 两种语言的文件必须在同一次变更里对齐（`tools/check_repo.py` 会查）。

---

## 12. 真机验收清单（M7d）

建议先刷加速版（倍率 60）：emo 的 4 小时只要 4 分钟，一个宠物日只要 24 分钟。

| 验收项 | 怎么测 | 应该看到 |
| --- | --- | --- |
| 旧存档迁移 | 在已有 v1 存档的设备上分段刷写 v2 固件，不擦 NVS | 噗噗状态保留，状态页零花钱 30 |
| 摸摸六档 | 玩耍页连续摸 6 次 | 1–3 小手拍头 + 开心跳；第 4 次皱眉常驻 +"够了啦"；第 5 次跳开、小手弹飞、"开心 −5"；第 6 次无小手无气泡 |
| 窗口复位 | 等 10 分钟宠物时间 | 皱眉消失，再摸又是第 1 次 |
| 打工五档 | 连续打工 6 次 | 1–2 接住表格 +10 元；第 3 次砸头 + emo 气泡；4–5 砸头 +3 元；第 6 次"做不动了" |
| emo 表情和优先级 | emo 时回首页；再等饱腹降到 < 15（加速版几分钟）对比 | 首页 emo 待机；很饿时改趴着 |
| emo 解除 | 喂星星糖（饱腹 ≥ 90 时也试） | 立刻恢复；饱腹 ≥ 90 时星星糖照吃 |
| emo 自然解除 | 加速版等 4 分钟，或深睡后醒来 | 恢复普通待机 |
| 没钱 | 把钱花光再喂饭团 | 摇头 +"没钱啦，去打工吧"，钱不变 |
| 羊过门普通 / 学霸 | 各玩两局 | 卡片两行都在卡内、不截断；倒计时条走完卡片滑过去；每波有"最佳 / 还行 / 差了"；狼群数字滑入；结果牌正确 |
| 性能日志 | 看串口 | 每秒最少帧 ≥ 30，一帧最长 ≤ 40 ms，换道到画出 ≤ 50 ms，LVGL 池剩余 ≥ 4 KB（接星星实测约 37 KB） |
| 字库 | 看串口开机日志 | `pupu_fonts_selfcheck` 缺字 0 |
| 音效 | 听 | 啪、哼、三连降、叮叮、砰、叹气、狼嚎都能分辨 |

---

## 13. 后续（不在本次范围）

- **M8 微信小程序**：协议不变，直接按 `tools/pupu-sync-web/pupu_protocol.js` 实现。到那时再决定要不要把零花钱和 emo 放进 STATUS（`flags` 的 bit0 留给 emo，`proto_ver` 升 2）。本次所有功能都不依赖手机。
- 正式美术替换临时像素图（Piskel，流程见 `assets/images/pupu/README.zh_CN.md`）。
- 真机试玩后可能要调的数字都集中在 `pupu_config.h` 和 `gate_model.c` 开头的调参结构体里。

---

<details>
<summary>附录：审稿推演摘要（为什么是这些数字）</summary>

- **衰减**：v1 每 6 分钟饱腹 −1，8 小时 −80，加上很饿时开心每小时 −16，除非离开前接近满（100 / 100 也只剩 20 / 36），第二天拿起来大多是 10 / 10。改成每 15 分钟 −1 后，一个饭团 + 三次摸摸的会话之后，8 小时后到达约饱腹 65–70 / 开心 25–45。
- **打工封顶**：不封顶时进 emo 后连按 30 次 = +90 元，第二天就到上限。封在每天 5 次后，emo 打工两次 = 6 元 = 一个饭团，零花钱 0 又 emo 又饿的玩家仍能自救，不会死局。不采用"emo 打工 −3 元"，因为那样 0 元的玩家当天只能干等。
- **打工占额度**：一天 0 / 1 / 2 / 3 次打工 → 当天亲密上限 20 / 17 / 14 / 11 → 大约 5 / 6 / 7 / 9 天长大。钱和成长真正对立。
- **摸摸亲密保持 +1**：三次会话 9 点 + 饿时饭团最多 9 点 = 18 < 20，小游戏和同步对成长才有意义；若改 +2 / +1 / +1 则 21 ≥ 20，其他来源全部无效。
- **羊过门门槛**：用草稿卡池模拟 2000 局，"最优路径 70%"下错 1 波赢 71%、错 2 波只有 26%，而且错在前几波和后几波结果相反。改成按累计表现比 `R ≥ 600` 并约束"次好 ≥ 80%"后，难度恒定为"次好最多错两次"。
- **卡片静止**：按接星星 131 px/s 的速度，80 px 卡片完整在屏内只有 1.2 秒，三张卡读不完；静止 + 倒计时条才有 4–8 秒。
- **emo 用倒计时不用"到第二天"**：宠物日边界从孵化时刻起每 24 小时一跳，界面看不到，同样的三次打工代价会在 0 到 8 点亲密之间随机。
- **星星糖绕过拒食**：常见顺序是先喂饱再打工，饱腹 ≥ 90 时 emo 的唯一快捷出口会被"吃不下啦"挡 1 小时。

</details>

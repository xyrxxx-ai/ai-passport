<p align="right">
  <strong>简体中文</strong> · <a href="progress.md">English</a>
</p>

# 噗噗 v1 开发进度

每个里程碑结束时更新这份文档。产品说明见 [design.zh_CN.md](design.zh_CN.md)；里程碑的具体内容见开发计划（仓库外）第 10 节。

## 当前状态

- **分支**：`feature/pupu-v1`，从 `origin/main` 的 `006c227` 建出。没有设置上游跟踪分支，也从不 push。
- **已完成**：M0 准备、M1 宠物规则核心。
- **下一个里程碑**：M2 第一屏：看到噗噗（需要设备）。
- **提交状态**：M0（`2c77a38`）和 M1 都已提交到本地 `feature/pupu-v1`。M1 的提交号在下一次更新时补记（一个提交没法写下自己的提交号）。没有 push。

## 里程碑

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| M0 | 准备：建分支、第一次编译、完整检查基线、文档入仓库 | 完成 |
| M1 | 宠物规则核心 | 完成 |
| M2 | 第一屏：看到噗噗 | 未开始 |
| M3 | 照顾功能、音效、存档 | 未开始 |
| M4 | 熄屏、深睡和时钟连续 | 未开始 |
| M5 | 小游戏「接星星」 | 未开始 |
| M6 | 蓝牙同步和 Mac 网页 | 未开始 |
| M7 | 微信小程序 | 未开始 |
| M8 | 整机验收和收尾 | 未开始 |

## M0 准备（2026-10-23）

### 做了什么

- 新建分支 `feature/pupu-v1`。
- 确认 5 个 passport 技能已经出现在 Claude Code 的可用技能列表里。
- 在这台 Mac 上第一次编译原版 main：`idf.py set-target esp32c3 && idf.py build` 通过，在沙盒里就能跑。
- 在沙盒外跑完整检查 `./tools/validate.sh`，作为基线。
- 产品说明移进仓库，存成本目录下的 `design.md` 和 `design.zh_CN.md`；新建本进度文档；在仓库根目录新建 `README.md` 和 `README.zh_CN.md`。

### 基线编译身份

固件代码和 `origin/main` 的 `006c227` 完全相同，M0 没有改任何固件代码。

| 项目 | 值 |
| --- | --- |
| 固件版本号（app descriptor） | `006c227`，ESP-IDF v5.5.3，不带 `-dirty` |
| 本地增量镜像 `build/FoloToy-AI-Passport.bin` | SHA-256 `596f24bc26f60865116f1effbd17978fb98e9f5d1c0fa69867bcaf2f6e03a2b7` |
| 完整检查生成的 `build/FoloToy-AI-Passport-full.bin` | SHA-256 `5831fc0bb324eac7f41430b2a77ca5a59ffadf87b1a3bb591fea3653f94aa70e`（1593152 字节，从 `0x0` 刷） |
| 对应的 ELF | SHA-256 `28aa1bdef91d4ccad6fe920507b2db57c51bacbf77cfde5ace336bf23bfa320f` |
| 调试文件包 | `build/firmware/5831fc0bb324eac7f41430b2a77ca5a59ffadf87b1a3bb591fea3653f94aa70e/`，`archive_firmware.py verify` 通过 |
| 应用大小 | 1527616 / 8323072 字节（factory 分区用了约 18%） |
| 刷进设备的镜像 | 无，M0 没有刷机 |

### 结果

```text
Build: PASS（idf.py build 在沙盒里通过；validate.sh 的固件编译、合并镜像、分区布局和打包校验都通过）
Host tests: PASS（validate.sh 的仓库检查、actionlint 和全部主机测试通过；需要下面两个 macOS 绕过办法）
Device tests: NOT RUN（设备没有连接，M0 没有刷机）
Unverified: 基线固件没有在真机上跑过；validate.sh 在这台 Mac 上必须配合下面两个绕过办法才能跑完
```

### 备注

- 沙盒不允许 git 写 `.git/config`，所以分支是用 `git switch --no-track -c` 建的，没有上游跟踪。这不影响开发，还能避免误上传。
- `tools/install_passport_skills.py --check` 只检查 Codex 用的 `.agents/skills`，对 Claude Code 一定会报缺失，不能用它判断技能有没有装好。

### 这台 Mac 上跑 validate.sh 的两个坑（仓库脚本没改）

仓库的 CI 跑在 Linux 上，所以下面两个问题上游没发现。M0 没有改仓库脚本，只在仓库外绕过：

1. **actionlint 校验失败**：macOS 26 自带的 `/sbin/sha256sum` 不认识 `--check --status`，`tools/install-actionlint.sh` 下载完就报 `usage: sha256sum`。
   - 绕过：用 `shasum -a 256` 手动核对 `/tmp/actionlint-1.7.12/actionlint_1.7.12_darwin_arm64.tar.gz`，结果必须等于脚本里写的 `aba9ced2dee8d27fecca3dc7feb1a7f9a52caefa1eb46f3271ea66b6e0e6953f`，一致后再解压出 `actionlint`。解压后脚本会直接用它。`/tmp` 重启后会清空，到时要重做一遍。
2. **四个 demo 运行时测试链接失败**：`validate.sh` 用了 Linux 链接器参数 `-Wl,--gc-sections`，苹果链接器报 `unknown options: --gc-sections`。
   - 绕过：在仓库外放一个小脚本，把这个参数换成苹果的等价写法 `-Wl,-dead_strip`，其他参数原样交给 `/usr/bin/cc`，再用 `CC=<脚本路径> ./tools/validate.sh` 运行。换过之后四个测试都通过。
- 以后可以考虑直接在仓库里修好这两个脚本，让它们在 macOS 上也能跑。这会改上游的工具文件，要先征得你同意。

## M1 宠物规则核心（2026-10-23）

### 做了什么

- 新增 4 个纯逻辑模块，只用标准 C 头文件，不引用 ESP-IDF 和 LVGL：
  - `main/pupu_config.h`：所有规则数值集中放在一处，真机试玩后在这里调。
  - `main/pupu_model.{c,h}`：宠物状态和规则。包括每分钟衰减、离开补算（8 小时上限、最低 10）、宠物日和每日上限、三种食物、摸摸头、零食和摸摸的窗口、小游戏结算、首次同步奖励、孵化和长大。每个动作都返回"实际变化了多少"，结果页可以直接拿来提示玩家。
  - `main/pupu_save_codec.{c,h}`：存档逐字段编码。小端定长，带 magic、版本号、序号、长度和 CRC32，一条记录 67 字节。
  - `main/pupu_clock.{c,h}`：真实时间换算成宠物时间（倍率和不满 1 秒的余数）、开机原因分类、RTC 连续性检查、手机对时和 `acct_now` 记账。
- 新增 3 个主机测试 `tests/test_pupu_model.c`、`tests/test_pupu_save_codec.c`、`tests/test_pupu_clock.c`，已手动登记到 `tools/validate.sh`。
- 3 个新的 `.c` 已加进 `main/CMakeLists.txt` 的 SRCS，设备的 RISC-V 编译器也编译通过。固件里还没有任何地方调用它们，所以**设备上看不出变化**。

### 测试覆盖了什么

计划书 M1 列出的每一项都有对应的测试：

- 每小时衰减（饱腹 −10、开心 −8）；很饿时开心 −16，并测了饱腹正好 15 和 14 的边界；
- 补算 8 小时上限；补算最低 10；本来就低于 10 的数补算后不变；
- 补算中跨过宠物日，每日计数清零；离开 3 天时数值只补 8 小时，天数前进 3 天；
- 第 4.3 节的 5 个时间场景，用一个"模拟设备"走完整的存档、断电或深睡、开机、同步流程（存档真的经过编码和解码）；另外加了 3 个场景：断电重启后还没存档又重启、从没同步过的设备深睡醒来、时间加速下的离开；
- 每日上限：亲密 20（多个来源合计）、星星糖 3、首次同步 +2；
- 零食 60 分钟窗口（测了第 59 和第 60 分钟）、摸摸 10 分钟窗口（躲开时不计数、不延长）；
- 吃饱拒食（三种食物、什么都不变）；饿时喂饭团加亲密；
- 孵化；亲密满 100 长成成年（喂食、摸摸、小游戏、同步都能触发，只触发一次）；
- 小游戏结算：中途退出、没到 8 颗、赢、饱腹扣到 0、第 4 次赢、星星糖存满 99；
- 存档：编码往返、每个字节损坏都能发现、长度错误、magic 错误、版本迁移、未来版本拒绝；
- CRC32 用 "123456789" 算出 `0xCBF43926`，和 Python `zlib.crc32` 一致；
- 倍率换算和余数进位；开机原因分类（包括 USB、TASK_WDT）；早于 2026 的手机时间被拒绝；用错误的时间对时后能纠正。

另外做了一次"变异检查"：在临时副本里故意把 12 处规则改坏（比如去掉最低 10、去掉 8 小时上限、把 USB 重启当成断电、先看版本号再验 CRC），确认测试每次都会失败。仓库里的文件没有被改动。

### 存档的版本迁移

v1 是第一个存档版本，现在还没有旧版本需要迁移。所以测试是这样做的：

- 用 Python（`struct` + `zlib`）**独立**生成了一份 v1 存档的逐字节样例，写在测试里。固件必须能读出它，并且写出完全相同的字节。以后改存档格式时，这份样例就是"旧存档还能读"的证明，不能删。
- 未来版本的存档：报 FUTURE，不改输出，调用方据此不覆盖它。
- 版本 0 拒绝；损坏的记录哪怕版本号看起来更新，也只算损坏，不会挡住覆盖。
- 真正的"旧版本升级"测试要等出现 v2 时再写。到时候必须同时：版本号加 1、在 `decode_payload()` 里给 v1 加一个分支、保留上面的 v1 样例并测试它能升级。

### 写代码时做的几个规则解释（你可以否决）

产品说明没有写到这么细，下面是按"轻松陪伴"的思路定的：

1. 蛋阶段不能喂、摸、玩，也不领首次同步奖励（因为"蛋阶段数值不变"）。孵化后当天第一次同步还能领。
2. 长成成年以后，亲密继续增加（"只增不减"），每天仍然最多 +20。
3. 吃腻的饼干（窗口里第 4 块起）照样加饱腹，也算进次数；只有被拒绝的饼干不算。
4. 没有星星糖时选星星糖，先提示"没有星星糖"，不管饱不饱。
5. 小游戏赢了但星星糖已经存满 99：不给，也不占当天 3 颗的名额。
6. 窗口按整分钟计时：如果第一块饼干是在某一分钟的中间吃的，窗口实际比 60 分钟短不到 1 分钟。

### 给后面里程碑的接口说明

- **开机原因**：`pupu_reset_reason_t` 的编号和 ESP-IDF 5.5.3 的 `esp_reset_reason_t` 完全一致。M4 的设备代码要先用 `_Static_assert` 确认两边一致，再做转换。
- **会话令牌**：只在连续性检查不通过时换新令牌，通过时沿用存档里的令牌。`test_restart_before_first_save` 覆盖了"断电后还没存档又重启"的情况。
- **同步时的调用顺序**：`pupu_clock_sync()` → `pupu_model_catch_up(pupu_clock_to_pet(离开秒数))` → `pupu_model_apply_sync()`。
- **留给 M3**：两个存档槽怎么选（序号更新、校验通过；遇到 FUTURE 不覆盖）放在 `pupu_store`。
- **留给 M4、M6**：STATUS 里的 `away_min` 和"你回来啦"要用的"最近一次离开时长"，由调用方保存 `pupu_clock_boot()` 和 `pupu_clock_sync()` 返回的离开秒数，M1 没有存。

### 编译身份

| 项目 | 值 |
| --- | --- |
| 源码 | `2c77a38` 加上未提交的 M1 改动 |
| 固件版本号（app descriptor） | `2c77a38-dirty`，ESP-IDF v5.5.3（`-dirty` 表示有未提交的改动） |
| 本地增量镜像 `build/FoloToy-AI-Passport.bin` | SHA-256 `58002a38bf65b00b3ea53b967cdbca11e2d63f41b691d967578f41c4e512c752` |
| 完整检查生成的 `build/FoloToy-AI-Passport-full.bin` | SHA-256 `64def7419d2643ba6fa902eb42ca392ad08b9379f394f3a697edda94a32eef9e`（1593152 字节，从 `0x0` 刷） |
| 对应的 ELF | SHA-256 `5cb61a2e5b1df1073980612d12d8b2e9b3bef152b2e64fe092cc92dd6e731632` |
| 调试文件包 | `build/firmware/64def7419d2643ba6fa902eb42ca392ad08b9379f394f3a697edda94a32eef9e/`，`archive_firmware.py verify` 通过 |
| 应用大小 | 1527616 / 8323072 字节，和 M0 一样：还没有代码调用新模块，链接时被丢掉了 |
| 刷进设备的镜像 | 无，M1 没有刷机 |

### 结果

```text
Build: PASS（idf.py build 在沙盒里通过，3 个新文件由设备编译器编译、没有警告；validate.sh 的固件编译、合并镜像、分区布局和打包校验都通过）
Host tests: PASS（validate.sh 的仓库检查、actionlint 和全部主机测试通过，包括 3 个新测试；仍需 M0 记录的两个 macOS 绕过办法）
Device tests: NOT RUN（M1 只有纯逻辑，设备上看不出变化，没有刷机）
Unverified: 新规则还没有接到固件里，真机上的效果要到 M2–M4 才能看到；RTC 在深睡和各种重启后是否真的连续，要到 M4 真机确认
```

## 下一步

M2 第一屏：看到噗噗。第一件事是真机试验（I4 画布换色、RGB565A8 拉伸、显示一行中文），**需要先征得你同意刷机**。开始 M2 之前请准备：

- 一根能传数据的 USB-C 线，开新对话前把设备插到电脑上；
- 看一下设备上 ↑ 键是不是在 ↓ 键上面（小游戏的方向依赖这一点）；
- 到时候同意下载字体文件（几 MB，需要联网）。

跑完整检查仍然需要上面 M0 记录的两个 macOS 绕过办法。

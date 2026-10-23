<p align="right">
  <strong>简体中文</strong> · <a href="progress.md">English</a>
</p>

# 噗噗 v1 开发进度

每个里程碑结束时更新这份文档。产品说明见 [design.zh_CN.md](design.zh_CN.md)；里程碑的具体内容见开发计划（仓库外）第 10 节。

## 当前状态

- **分支**：`feature/pupu-v1`，从 `origin/main` 的 `006c227` 建出。没有设置上游跟踪分支，也从不 push。
- **已完成**：M0 准备。
- **下一个里程碑**：M1 宠物规则核心（纯逻辑和主机测试，不需要设备）。
- **提交状态**：M0 已提交到本地 `feature/pupu-v1`（父提交 `006c227`，提交号用 `git log -1` 查），没有 push。

## 里程碑

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| M0 | 准备：建分支、第一次编译、完整检查基线、文档入仓库 | 完成 |
| M1 | 宠物规则核心 | 未开始 |
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

## 下一步

M1 宠物规则核心：`pupu_config.h`、`pupu_model`、`pupu_save_codec`、`pupu_clock` 里的纯换算部分，加上主机测试，并登记到 `tools/validate.sh` 和 `main/CMakeLists.txt`。M1 不需要连接设备。

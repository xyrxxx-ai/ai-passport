<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 噗噗像素图

噗噗宠物应用的美术源文件。固件不直接读这些文件：`tools/gen_pupu_assets.py` 会把它们转成
`main/pupu_assets.c` 和 `main/pupu_assets.h`。生成结果也提交进仓库，所以编译时不需要 Pillow。

## 文件

| 文件 | 格式 | 用途 |
| --- | --- | --- |
| `pupu.gpl` | GIMP 调色板，16 色 | 所有帧只能用这些颜色，每个色号有固定用途（见下表） |
| `egg_idle.png` | 96 × 48 PNG RGBA，2 帧 | 蛋待机 |
| `egg_wobble.png` | 96 × 48 PNG RGBA，2 帧 | 玩家按住捂热时，蛋在摇晃 |
| `egg_hatch.png` | 144 × 48 PNG RGBA，3 帧 | 孵化 |
| `child_idle.png` | 96 × 48 PNG RGBA，2 帧 | 幼年噗噗站着眨眼 |
| `child_hungry.png` | 96 × 48 PNG RGBA，2 帧 | 幼年噗噗很饿时趴着 |
| `adult_idle.png` | 96 × 48 PNG RGBA，2 帧 | 成年噗噗站着眨眼 |
| `adult_hungry.png` | 96 × 48 PNG RGBA，2 帧 | 成年噗噗很饿时趴着 |
| `child_eat.png`、`adult_eat.png` | 各 96 × 48 PNG RGBA，2 帧 | 咀嚼（循环放约 2 秒） |
| `child_happy.png`、`adult_happy.png` | 各 96 × 48 PNG RGBA，2 帧 | 开心地跳一下，喷出一小团"噗" |
| `child_dodge.png`、`adult_dodge.png` | 各 96 × 48 PNG RGBA，2 帧 | 摸太多次时躲开 |
| `child_shake.png`、`adult_shake.png` | 各 96 × 48 PNG RGBA，2 帧 | 太饱时摇头不吃 |
| `child_wave.png`、`adult_wave.png` | 各 96 × 48 PNG RGBA，2 帧 | 同步好了，抬起前腿挥手 |
| `child_annoyed.png`、`adult_annoyed.png` | 各 96 × 48 PNG RGBA，2 帧 | 不耐烦：皱眉侧头（摸摸第 4 次起，保持到窗口结束） |
| `child_angry.png`、`adult_angry.png` | 各 96 × 48 PNG RGBA，2 帧 | 激烈反抗：生气跳开（摸摸第 5 次） |
| `child_work.png`、`adult_work.png` | 各 96 × 48 PNG RGBA，2 帧 | 打工：抱着一张表格、冒汗 |
| `child_dizzy.png`、`adult_dizzy.png` | 各 96 × 48 PNG RGBA，2 帧 | 被表格砸晕：倒地、头顶转圈 |
| `child_emo.png`、`adult_emo.png` | 各 96 × 48 PNG RGBA，2 帧 | emo 待机：垂头，头顶一朵小乌云 |
| `icon_hand.png` | 48 × 48 PNG RGBA，1 帧 | 摸摸时从上方落下的小手（照顾页叠层） |
| `icon_sheet.png` | 48 × 48 PNG RGBA，1 帧 | 打工时掉下来的 Excel 表格（照顾页叠层） |
| `icon_wolf.png` | 48 × 48 PNG RGBA，1 帧 | 羊过门结算时滑进来的狼群 |

每个动画文件高 48 像素，每帧 48 × 48，从左到右排成一行，只能用调色板里的颜色。图标是单张 48 × 48 的图，
按原色读入、不查调色板（它们转成 RGB565A8，也不参与毛色变化），所以可以用肤色、绿色、狼灰这些调色板里
没有的颜色。现在的图都是 `tools/make_placeholder_art.py` 用简单形状画的临时美术。来源：为本仓库生成，
没有使用第三方素材。

## 调色板色号

| 色号 | 用途 | 色号 | 用途 |
| --- | --- | --- | --- |
| 0 | 透明 | 8 | 羊毛亮部 |
| 1 | 轮廓 | 9 | 羊毛中间色 |
| 2 | 黑脸和黑蹄 | 10 | 羊毛暗部 |
| 3 | 脸上的高光 | 11 | 蛋壳 |
| 4 | 眼白 | 12 | 蛋壳阴影 |
| 5 | 腮红 | 13 | 护照墨水 |
| 6 | 卷角 | 14 | 金色（被砸晕时头顶转的小星星） |
| 7 | 卷角阴影 | 15 | 乌云灰（emo 时头顶的小乌云） |

饱腹和开心里较低的那个变低时，固件会把 8–10 号分 8 档混向灰色。所以羊毛只能用这三个色号，
别的地方都不要用。色号也定义在 `main/pupu_sprite_math.h` 里。

## 换成正式美术

1. 在 Piskel（免费，在浏览器里用）里导入 `pupu.gpl` 作为调色板，在 48 × 48 的画布上画。
   要保留蛋壳帽、卷角和圆团团的身形。
2. 每个动画导出成横向长条 PNG，文件名不变。背景用完全透明的像素；半透明像素会被拒绝。
3. 如果某个动画的帧数变了，要改 `tools/gen_pupu_assets.py` 开头 `ANIMS` 里这个动画的每帧时长。
   图标列在同一个脚本的 `ICONS` 里，每个带尺寸（小手、表格、狼群都是 48），尺寸不要改；
   图标可以用任何颜色，但每个像素要么完全透明、要么完全不透明。
4. 重新生成并检查：

   ```bash
   /usr/bin/python3 tools/gen_pupu_assets.py
   ```

   遇到调色板里没有的颜色，脚本会停下并报出文件名和坐标。它还会生成
   `build/pupu-preview.png`（放大 3 倍，包含每一档毛色），刷机前可以先看效果。
   如果改了图却没有重新生成 C 文件，`tests/test_pupu_assets_fresh.py` 会失败。

设备上每帧会放大 3 倍，拷进一块 144 × 144 的索引色（I4）画布，按 1:1 显示。像素数据放在 Flash 里，
每帧 1,152 字节。小游戏里的噗噗用同样的帧，在一块 48 × 48 的画布上按 1 倍显示。图标转成
RGB565A8（每个 6,912 字节），作为 LVGL 图片按 1:1 显示；小手和表格叠在宠物画布上方，由照顾页
每 50 ms 挪一次位置，狼群由小游戏页每 20 ms 挪一次。

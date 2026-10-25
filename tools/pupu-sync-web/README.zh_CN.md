<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 噗噗同步网页（Mac Chrome 原型）

一个网页，在 Mac 上的 Chrome 里用低功耗蓝牙和噗噗同步：把现在的时间发给设备（噗噗按你离开的时长补算），显示状态卡片，下载存档备份，然后结束同步。它先把协议跑通，再做微信小程序。

| 文件 | 是什么 |
| --- | --- |
| `index.html` | 网页本身。不用编译，也没有依赖。 |
| `pupu_protocol.js` | 消息编码和解码、CRC-32、重试、一次完整的同步。没有用浏览器接口，所以能用 Node 测试。 |
| `package.json` | 只是告诉 Node 这里的 `.js` 是 ES 模块。什么都不用安装。 |

协议本身写在 `main/pupu_sync_proto.h`。`tests/pupu_sync_vectors.txt` 里有逐字节的"标准答案"，固件测试 `tests/test_pupu_sync_proto.c` 和网页测试 `tests/test_pupu_sync_web.mjs` 都用它检查。

## 怎么用

1. 第一次用之前，在"系统设置 → 隐私与安全性 → 蓝牙"里允许 Chrome 使用蓝牙。
2. 在仓库根目录启动一个本地网页服务器，让它一直开着（按 Ctrl+C 停止）：

   ```bash
   python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/pupu-sync-web
   ```

3. 在 Chrome 里打开 `http://localhost:8000/`。不要去掉 `--bind 127.0.0.1`：网页蓝牙只在安全来源上能用，`localhost` 算安全来源。
4. 在设备的首页选「同步」。屏幕上会显示 `Pupu-XXXX` 和倒数。
5. 在网页上点「同步」，在 Chrome 弹出的列表里选同一个 `Pupu-XXXX`。

设备会等 60 秒让手机来连。连上以后，窗口一直开着，直到网页说"完成"，或者 20 秒没有新请求，最长 2 分钟。在设备上长按 OK 可以提前关掉窗口。

## 一次同步做了什么

1. 先打开 TX 特征的通知，再读一次它（`PUPU/<协议版本>/<固件版本>`）。
2. TIME：设备补算离开的时间（最多补 8 小时，补算部分最低降到 10），回复 STATUS。
3. SAVE_BEGIN，然后每 17 字节一块，一块一块 SAVE_GET。网页检查长度、SAVE_INFO 里的 CRC-32，以及存档记录末尾自带的 CRC。
4. DONE：设备回 DONE_ACK，噗噗挥手、"叮"一声，然后关掉蓝牙。

每个请求最多等 1 秒回复，没收到就用同一个序号重发，最多 3 次。设备把重发的请求当作同一个请求，不会重复补算。

## 遇到问题

- Chrome 的列表是空的：设备上的窗口已经关了，在设备上重新选「同步」。网页只列出名字以 `Pupu-` 开头的设备。
- 网页说这个浏览器不能用蓝牙：请用 Mac 上的 Chrome，并且从 `http://localhost:8000/` 打开，不要直接双击文件打开。
- 打开 `chrome://bluetooth-internals` 可以看连接情况；网页上的"通信记录"能看到收发的每一个字节。
- iPhone 上所有浏览器都不支持网页蓝牙。手机版是微信小程序（M7 里程碑）。

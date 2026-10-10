<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**让已越狱的 PS5 用上蓝牙耳机：游戏和系统声音，无需适配器。**

开发者： **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 **中文** · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 是一个 payload（ELF），通过 PS5 自带的蓝牙把主机声音传到普通蓝牙耳机或音箱。你可以在主机提供的网页上控制它。它不会改动游戏、固件或越狱。

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 网页" width="900"></p>

## 需要

- 已越狱的 PS5，并在端口 **9021** 运行 ELF 加载器（elfldr）。
- 支持 A2DP（SBC，48 kHz 立体声）的蓝牙耳机或音箱。
- 同一网络中的浏览器（PS5、手机或电脑）。

## 安装和运行

1. 从[最新版本](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest)下载 **HearBridge-PS5-1.3.0.elf**。
2. 发送给加载器： `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. 打开 **http://&lt;console-ip&gt;:8090**，或主屏幕上的 **HearBridge** 图块。
4. 打开 **设置**，再进入 **耳机**。让耳机进入配对模式，按 **搜索设备**（约 12 秒），然后按 **连接**。

关闭网页后 HearBridge 仍在运行。再次发送 ELF 会替换正在运行的副本，**停止 HearBridge** 会结束它。

## 网页

- **耳机**：电量百分比（通过免提链路读取）、信号和链路质量。**切换耳机** 一键切换到另一副已保存的耳机。
- **声音**：带预设的 5 段均衡器、最高 500 % 增益、跟随耳机按键的耳机音量、**静音** 和 **夜间模式**（爆炸声更小，人声更清楚）。
- **连接**：编解码器，以及 40 到 200 ms 的延迟滑块和实时延迟估算。
- **日志**：每一步都有颜色：绿色成功，红色失败，蓝色是你的操作。

**设置** 打开侧边菜单：

- **主页**：回到主页面。
- **耳机**：搜索、连接、断开和忘记。
- **声音与游戏**：测试音、纯净声音、**耳机的下一首/上一首调节音量**（适合没有音量键的耳机）以及已保存的游戏。
- **详细信息**：格式、电量、AVRCP、延迟明细和蓝牙芯片。
- **备份与恢复**：已保存的耳机（含配对信息）、设置和游戏配置都在一个文件里，可存到 U 盘、主机或你正在浏览的设备上。

### 游戏配置

启动游戏，调好你喜欢的声音并保存。配置按游戏、按耳机分别保存，游戏启动时自动生效，关闭后恢复你平时的声音。声音 面板上的 **更新游戏配置** 会保存你的修改。

### 自动连接和休息模式

已保存的耳机在开机或从充电盒取出时会自动连接。进入休息模式前 HearBridge 会干净地停止耳机，唤醒后重新连接。

### 编解码器

**自动** 就是普通 SBC，什么设备都能用。耳机支持时可以手动选择 **SBC HQ** 和 **SBC-XQ**。 不支持 AAC、aptX 和 LDAC。

## 我的是哪种芯片？

同一个文件支持两种蓝牙芯片。HearBridge 会自动识别芯片，并显示在 **详细信息** 中。

| 型号 | 蓝牙芯片 |
|---|---|
| CFI-10xx (首发) | Marvell/NXP |
| CFI-11xx, CFI-12xx (标准版) | Marvell/NXP 或 MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP 或 MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

已在 PS5 CFI-10xx（Marvell/NXP）和 PS5 Slim CFI-2008（MediaTek）上测试。欢迎反馈其他型号的情况。

来源: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights PS5 Pro 拆解](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## 已知限制

- 一次只能连一副耳机，不支持麦克风。电视也会继续出声。
- 一次只运行一个使用蓝牙的 payload。DualSense 照常可用。
- 已测试耳机：Sony WF-1000XM6、OnePlus Buds Ace 2、Xbox Wireless Headset。

## 报告问题

payload 运行时应出现 **"HearBridge &lt;version&gt;: starting"** 通知。如果没有，说明加载器没有运行它。

1. 通过 FTP 获取 `/data/hearbridge/hearbridge.log` 和 `diag.txt`（例如 [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)，端口 2121）。`diag.txt` 也可以在 http://&lt;console-ip&gt;:8090/api/diag 获取。
2. 打开一个 [GitHub issue](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose)，附上主机型号、固件、加载器、HearBridge 版本和日志文件。

## 构建

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

## 致谢

MediaTek 扫描暂停与 USB 管道修复，以及 HCI 调试工具，作者 [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## 许可证

GPL-3.0-or-later。参见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

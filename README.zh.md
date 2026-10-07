<div align="center">

# HearBridge PS5

**为破解的 PS5 连接蓝牙耳机：游戏和系统声音，无需适配器。**

开发者： **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 **中文** · 🇮🇹 [Italiano](README.it.md) · 🇮🇱 [עברית](README.he.md)

</div>

HearBridge PS5 是用于破解 PS5 的载荷（ELF）。它采集主机的声音，并通过主机自带的蓝牙模块传输到普通的蓝牙耳机或音箱（A2DP、SBC）。通过主机提供的网页进行控制。它不会修改游戏、固件或破解环境。

<p align="center"><img src="docs/img/ui-en.png" alt="HearBridge PS5 网页" width="520"></p>

## 要求

- 已破解的 PS5，并有在端口 **9021** 监听的 ELF 加载器（elfldr）。
- 支持 **A2DP**（SBC）的蓝牙耳机或音箱。
- 同一网络中的 PS5 浏览器、手机或电脑。

## 安装与运行

1. 从[发布页](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/tag/v1.0.1)下载 **HearBridge-PS5-1.0.1.elf**。
2. 将其发送到主机的加载器，例如：
   `socat -u FILE:HearBridge-PS5-1.0.1.elf TCP:<console-ip>:9021`
3. 打开 **http://&lt;console-ip&gt;:8090**（或首次运行时添加到主屏幕的 **HearBridge** 图块）。

## 使用

- **配对：**让耳机进入配对模式，按 **搜索设备**（约 20 秒），然后按耳机旁边的 **连接**。耳机会被保存，声音开始播放。
- **连接：**已保存的设备只在你按下其所在行的 **连接** 时才会连接。后台不会自动连接任何设备。
- **断开：**断开连接，设备仍保留。如果耳机放回充电盒或超出范围，页面会显示 **未连接**，直到你再次按连接。
- **忘记：**删除设备及其配对密钥。
- **音量：**增益滑块（软件放大，最高 500%）和耳机音量（AVRCP 绝对音量，也会跟随耳机自身的按键）。**静音** 和 **测试音** 便于检查。
- **停止 HearBridge** 可干净地结束载荷。再次加载 ELF 前请先使用它。

网页支持 11 种语言，包括从右到左书写的希伯来语和阿拉伯语。

## 说明

- 使用 PS5 内置蓝牙；DualSense 照常工作。同一时间只运行一个使用蓝牙的载荷。
- 仅支持 SBC 编码，一次一个设备，不支持麦克风。电视也会继续播放声音。
- 已在 Sony WF-1000XM6、OnePlus Buds Ace 2 和 Xbox Wireless Headset 上测试。
- 设置、已保存的设备和日志位于 `/data/hearbridge/`（`hearbridge.log` 记录每一步）。

## 编译

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make ps5
make test
```

## 许可证

GPL-3.0-or-later。参见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

---

<p align="center">Developed by X-F1REBALL-X</p>

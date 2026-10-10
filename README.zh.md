<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**让已越狱的 PS5 用上蓝牙耳机：游戏和系统声音，无需适配器。**

开发者： **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 [العربية](README.ar.md) · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 **中文** · 🇮🇹 [Italiano](README.it.md)

</div>

HearBridge PS5 是一个 payload（ELF），通过主机自带的蓝牙，把 PS5 的游戏和系统声音送到普通蓝牙耳机。你可以在主机提供的网页上控制它。

<p align="center"><img src="docs/img/ui-tv.png" alt="HearBridge PS5 网页" width="440"> <img src="docs/img/ui-settings.png" alt="HearBridge PS5 网页" width="440"></p>

## 亮点

- 一个文件适用于所有 PS5，Marvell/NXP 或 MediaTek 蓝牙都行。
- 游戏配置：声音跟随游戏和耳机自动切换。
- 电量百分比、夜间模式和一键切换耳机。

## 安装

1. 从[最新版本](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest)下载 **HearBridge-PS5-1.3.0.elf**。
2. 发送到端口 9021 上的 ELF 加载器。
3. 打开 **http://&lt;console-ip&gt;:8090** 或 **HearBridge** 图块，然后在 **设置** 中配对耳机。

从功能到故障排除，其他内容都在 **[指南](https://x-f1reball-x.github.io/HearBridge-PS5/)** 里。

## 构建

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

## 致谢

MediaTek 扫描暂停与 USB 管道修复，以及 HCI 调试工具，作者 [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## 许可证

GPL-3.0-or-later。参见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

[在 Ko-fi 上支持本项目](https://ko-fi.com/xf1reballx)。本项目将继续免费并保持开源。

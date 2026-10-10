<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**سماعات بلوتوث لجهاز PS5 مكسور الحماية: صوت الألعاب والنظام، بدون دونجل.**

تطوير **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 **العربية** · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

<div dir="rtl">

HearBridge PS5 حمولة (ELF) تشغّل صوت الألعاب والنظام في PS5 على سماعات بلوتوث عادية، باستخدام بلوتوث الجهاز نفسه. تتحكم بها من صفحة ويب يقدمها الجهاز.

<p align="center"><img src="docs/img/ui-tv.png" alt="صفحة ويب HearBridge PS5" width="900"></p>

## أبرز الميزات

- ملف واحد لكل أجهزة PS5، ببلوتوث Marvell/NXP أو MediaTek.
- ملفات الألعاب: صوتك يتبع اللعبة والسماعة.
- نسبة البطارية والوضع الليلي وتبديل السماعة بلمسة واحدة.

## التثبيت

1. نزّل **HearBridge-PS5-1.3.0.elf** من [أحدث إصدار](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. أرسله إلى محمّل ELF على المنفذ 9021.
3. افتح **http://&lt;console-ip&gt;:8090** أو مربع **HearBridge**، ثم اقرن سماعاتك من **الإعدادات**.

كل ما تبقى، من الميزات إلى حل المشكلات، موجود في **[الدليل](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## البناء

</div>

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test
```

<div dir="rtl">

## شكر

إيقاف البحث مؤقتا في MediaTek وإصلاح قناة USB وأداة تصحيح HCI، بواسطة [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## الترخيص

GPL-3.0-or-later. انظر [LICENSE](LICENSE) و[NOTICE](NOTICE).

</div>

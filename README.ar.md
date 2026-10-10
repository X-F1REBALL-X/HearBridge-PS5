<div align="center">

<img src="assets/icon0.png" width="128" alt="HearBridge icon">

# HearBridge PS5

**سماعات بلوتوث لجهاز PS5 مكسور الحماية: صوت الألعاب والنظام، بدون دونجل.**

تطوير **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 **العربية** · 🇪🇸 [Español](README.es.md) · 🇫🇷 [Français](README.fr.md) · 🇩🇪 [Deutsch](README.de.md) · 🇧🇷 [Português](README.pt.md) · 🇷🇺 [Русский](README.ru.md) · 🇯🇵 [日本語](README.ja.md) · 🇨🇳 [中文](README.zh.md) · 🇮🇹 [Italiano](README.it.md)

</div>

<div dir="rtl">

HearBridge PS5 حمولة (ELF) تنقل صوت الجهاز عبر بلوتوث PS5 المدمج إلى سماعات أو مكبرات بلوتوث عادية. تتحكم بها من صفحة ويب يقدمها الجهاز. لا تغيّر الألعاب ولا البرنامج الثابت ولا كسر الحماية.

<p align="center"><img src="docs/img/ui-tv.png" alt="صفحة ويب HearBridge PS5" width="900"></p>

## المتطلبات

- جهاز PS5 مكسور الحماية مع محمّل ELF على المنفذ **9021** (elfldr).
- سماعات أو مكبر صوت بلوتوث يدعم A2DP (SBC، 48 كيلوهرتز ستيريو).
- متصفح على الشبكة نفسها (PS5 أو هاتف أو كمبيوتر).

## التثبيت والتشغيل

1. نزّل **HearBridge-PS5-1.3.0.elf** من [أحدث إصدار](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. أرسله إلى المحمّل: `socat -u FILE:HearBridge-PS5-1.3.0.elf TCP:<console-ip>:9021`
3. افتح **http://&lt;console-ip&gt;:8090** أو مربع **HearBridge** في الشاشة الرئيسية.
4. افتح **الإعدادات** ثم **السماعات**. ضع السماعات في وضع الاقتران، واضغط **البحث عن أجهزة** (حوالي 12 ثانية)، ثم **اتصال**.

إغلاق الصفحة يترك HearBridge يعمل. إرسال ملف ELF مرة أخرى يستبدل النسخة العاملة، و**إيقاف HearBridge** ينهيها.

## الصفحة

- **السماعة:** نسبة البطارية (تُقرأ عبر وصلة اليدين الحرتين)، والإشارة وجودة الاتصال. **تبديل السماعة** ينتقل إلى سماعة محفوظة أخرى بلمسة واحدة.
- **الصوت:** معادل من 5 نطاقات مع إعدادات جاهزة، وتعزيز حتى 500 %، ومستوى صوت السماعة يتبع أزرارها، و**كتم** و**الوضع الليلي** (انفجارات أهدأ وأصوات أوضح).
- **الاتصال:** الترميز وشريط زمن الاستجابة من 40 إلى 200 مللي ثانية مع تقدير مباشر للتأخير.
- **السجل:** كل خطوة بلون: الأخضر نجح، والأحمر فشل، والأزرق لضغطاتك.

**الإعدادات** تفتح قائمة جانبية:

- **الرئيسية:** العودة إلى الصفحة الرئيسية.
- **السماعات:** البحث والاتصال وقطع الاتصال والنسيان.
- **الصوت والألعاب:** نغمة اختبار وصوت نظيف و**التالي/السابق في السماعة يغيّر الصوت** (للسماعات بلا أزرار صوت) وألعابك المحفوظة.
- **التفاصيل:** التنسيق والبطارية وAVRCP وتفصيل زمن الاستجابة وشريحة البلوتوث.
- **النسخ الاحتياطي والاستعادة:** السماعات المحفوظة مع اقترانها والإعدادات وملفات الألعاب في ملف واحد، على وحدة USB أو الجهاز أو الجهاز الذي تتصفح منه.

### ملفات الألعاب

شغّل لعبة واضبط الصوت الذي تحبه واحفظه. تُحفظ الملفات لكل لعبة ولكل سماعة، وتعمل تلقائيا عند بدء اللعبة وتعيد صوتك المعتاد عند إغلاقها. **تحديث ملف اللعبة** في لوحة الصوت يحفظ تغييراتك.

### الاتصال التلقائي ووضع الراحة

تتصل السماعات المحفوظة وحدها عند تشغيلها أو إخراجها من العلبة. قبل وضع الراحة يوقف HearBridge السماعة بشكل نظيف ويعيد توصيلها بعد الاستيقاظ.

### الترميزات

**تلقائي** هو SBC العادي ويعمل مع كل شيء. يمكن اختيار **SBC HQ** و**SBC-XQ** يدويا عندما تدعمهما السماعة. AAC وaptX وLDAC غير مدعومة.

## ما الشريحة التي لدي؟

ملف واحد يعمل مع شريحتي البلوتوث. يتعرف HearBridge على الشريحة وحده ويعرضها في **التفاصيل**.

</div>

| الطراز | شريحة البلوتوث |
|---|---|
| CFI-10xx (الإصدار الأول) | Marvell/NXP |
| CFI-11xx, CFI-12xx (العادي) | Marvell/NXP أو MediaTek |
| CFI-20xx, CFI-21xx (Slim) | Marvell/NXP أو MediaTek |
| CFI-70xx, CFI-71xx (Pro) | MediaTek |

<div dir="rtl">

جُرّب على PS5 CFI-10xx (Marvell/NXP) وPS5 Slim CFI-2008 (MediaTek). نرحب بتقارير الطرازات الأخرى.

المصادر: [Sony compliance (BR)](https://www.playstation.com/pt-br/legal/compliance/) · [24Wireless](https://24wireless.info/playstation-5-cfi-1100-series) · [TechInsights تفكيك PS5 Pro](https://www.techinsights.com/blog/sony-playstation-5-pro-teardown)

## حدود معروفة

- سماعة واحدة في كل مرة، بدون ميكروفون. التلفاز يستمر في تشغيل الصوت أيضا.
- شغّل حمولة بلوتوث واحدة فقط في كل مرة. يد DualSense تبقى تعمل.
- السماعات المجرّبة: Sony WF-1000XM6 وOnePlus Buds Ace 2 وXbox Wireless Headset.

## الإبلاغ عن مشكلة

عند تشغيل الحمولة يجب أن يظهر إشعار **"HearBridge &lt;version&gt;: starting"**. إن لم يظهر، فالمحمّل لم يشغّلها.

1. نزّل `/data/hearbridge/hearbridge.log` و`diag.txt` عبر FTP (مثلا [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv)، المنفذ 2121). ملف `diag.txt` متاح أيضا على http://&lt;console-ip&gt;:8090/api/diag.
2. افتح [بلاغا على GitHub](https://github.com/X-F1REBALL-X/HearBridge-PS5/issues/new/choose) مع طراز الجهاز والبرنامج الثابت والمحمّل وإصدار HearBridge وملفات السجل.

## البناء

</div>

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # ps5-payload-sdk v0.43
make ps5      # dist/HearBridge-PS5-<version>.elf
make test     # host tests: cc, ffmpeg, python3 + numpy (node optional)
make send PS5_HOST=<console-ip>
```

<div dir="rtl">

## شكر

إيقاف البحث مؤقتا في MediaTek وإصلاح قناة USB وأداة تصحيح HCI، بواسطة [ZiZc3](https://github.com/ZiZc3) ([#6](https://github.com/X-F1REBALL-X/HearBridge-PS5/pull/6))

## الترخيص

GPL-3.0-or-later. انظر [LICENSE](LICENSE) و[NOTICE](NOTICE).

</div>

<div align="center">

<img src="docs/img/banner.png" alt="HearBridge PS5">

**سماعات بلوتوث لـ PS5 مكسور الحماية: صوت اللعبة والنظام، بدون دونغل.**

طوره **X-F1REBALL-X**

🇺🇸 [English](README.md) · 🇸🇦 **العربية**

</div>

**عائلة HearBridge:** [PS5](https://github.com/X-F1REBALL-X/HearBridge-PS5) · [PS4](https://github.com/X-F1REBALL-X/HearBridge-PS4) · [Browser](https://github.com/X-F1REBALL-X/HearBridge-Browser)

HearBridge PS5 هو ملف (ELF) يشغّل صوت اللعبة والنظام على سماعات بلوتوث عادية، عبر شريحة الكونسول أو محول USB. التحكم من صفحة ويب على الكونسول.

<p align="center">
<img src="docs/img/ui-tv.png" alt="واجهة HearBridge" width="440">
<img src="docs/img/ui-details.png" alt="تفاصيل HearBridge" width="440">
</p>
<p align="center">
<img src="docs/img/ui-adapter.png" alt="اختيار محول البلوتوث" width="640">
</p>

## أبرز المزايا

- ملف واحد لكل PS5 (Marvell/NXP أو MediaTek).
- **محولات USB:** Realtek RTL8761BU (فيرموير مدمج) وCSR8510، بالإضافة لشريحة الكونسول.
- **اختيار المحول** من الإعدادات مع تبديل فوري؛ اقتران واحد مشترك بين المحولات.
- واجهة شاشة واحدة لـ 1080p مع عناصر أكبر ولوحة تفاصيل (EQ والترميز والسجل).
- ملفات ألعاب، نسبة البطارية، الوضع الليلي وتبديل سريع للسماعة.

## التثبيت

1. نزّل **HearBridge-1.4.0.elf** من [آخر إصدار](https://github.com/X-F1REBALL-X/HearBridge-PS5/releases/latest).
2. أرسله إلى محمّل ELF على المنفذ 9021.
3. افتح **http://&lt;console-ip&gt;:8090** أو بلاطة **HearBridge**، ثم اربط السماعة من **الإعدادات**.

التفاصيل الكاملة في **[الدليل](https://x-f1reball-x.github.io/HearBridge-PS5/)**.

## محول بلوتوث USB

وصل عصا USB ويمكن لـ HearBridge استخدامها بدل شريحة الكونسول. العصا غير مشتركة مع DualSense، فيبقى يد التحكم يعمل أثناء البث.

المحولات المدعومة:

- **Realtek RTL8761BU** — الفيرموير مدمج في الـ ELF.
- **CSR8510** — محول HCI عادي بدون فيرموير إضافي.

من **الإعدادات → التفاصيل → محول البلوتوث** اختر تلقائي أو شريحة الكونسول أو العصا. الاقتران مشترك بين المحولات.

## الرخصة

GPL-3.0-or-later. انظر [LICENSE](LICENSE) و[NOTICE](NOTICE).

[ادعم المشروع على Ko-fi](https://ko-fi.com/xf1reballx). المشروع يبقى مجانياً ومفتوح المصدر.

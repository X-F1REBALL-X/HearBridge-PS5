#!/usr/bin/env python3
"""Draws HearBridge's home-screen tile icon (assets/icon0.png, 512x512):
headphones over a waveform, on the web page's dark/lime/ember palette.
Developed by X-F1REBALL-X."""
import math, sys
from PIL import Image, ImageDraw, ImageFilter

S, K = 512, 4                      # final size, supersampling factor
W = S * K
VOID, LIME, EMBER = (12, 11, 15), (200, 255, 61), (255, 106, 43)

def main(out):
    bg = Image.new("RGB", (W, W), VOID)
    d = ImageDraw.Draw(bg)
    for y in range(0, W, 8):                       # soft vertical vignette
        t = y / W
        c = tuple(int(VOID[i] + (30 - VOID[i]) * (1 - abs(t - .45) * 2) * .6) for i in range(3))
        d.rectangle([0, y, W, y + 8], fill=c)

    glow = Image.new("RGB", (W, W), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    art = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    a = ImageDraw.Draw(art)

    cx, cy, r, band = W // 2, int(W * .50), int(W * .30), int(W * .055)
    # headband arc
    box = [cx - r, cy - r, cx + r, cy + r]
    for dd in (a, g):
        dd.arc(box, 180, 360, fill=LIME, width=band)
    # ear cups (rounded rectangles) at both arc ends
    cw, ch = int(W * .13), int(W * .24)
    for sx in (cx - r, cx + r):
        cup = [sx - cw // 2, cy - int(ch * .15), sx + cw // 2, cy + int(ch * .85)]
        for dd in (a, g):
            dd.rounded_rectangle(cup, radius=cw // 2, fill=LIME)
        inner = [cup[0] + band // 2, cup[1] + band // 2, cup[2] - band // 2, cup[3] - band // 2]
        a.rounded_rectangle(inner, radius=cw // 3, fill=VOID + (255,))
    # waveform bars between the cups
    n, span = 9, int(r * 1.25)
    bw = span / n
    for i in range(n):
        h = (0.25 + 0.75 * abs(math.sin(i * 0.9 + 0.4))) * W * .17
        x0 = cx - span / 2 + i * bw + bw * .2
        col = EMBER if i == n // 2 else LIME
        rect = [x0, cy + W * .1 - h / 2, x0 + bw * .6, cy + W * .1 + h / 2]
        for dd in (a, g):
            dd.rounded_rectangle(rect, radius=int(bw * .3), fill=col)

    glow = glow.filter(ImageFilter.GaussianBlur(W * .02))
    base = bg.copy()
    base.paste(glow, (0, 0), glow.convert("L").point(lambda v: int(v * .55)))
    base.paste(art, (0, 0), art)
    base.resize((S, S), Image.LANCZOS).save(out, optimize=True)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/icon0.png")

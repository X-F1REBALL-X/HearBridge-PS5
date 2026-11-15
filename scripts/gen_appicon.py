#!/usr/bin/env python3
"""Embed the home screen icons (from assets/icon0.png) as C arrays in src/app_icon.h."""
import io, sys
from PIL import Image
src = sys.argv[1] if len(sys.argv) > 1 else "assets/icon0.png"
dst = sys.argv[2] if len(sys.argv) > 2 else "src/app_icon.h"
im = Image.open(src).convert("RGB")
def png(size):
    b = io.BytesIO()
    im.resize((size, size), Image.LANCZOS).save(b, "PNG", optimize=True)
    return b.getvalue()
def arr(name, data):
    rows = [", ".join("0x%02x" % c for c in data[i:i + 16]) for i in range(0, len(data), 16)]
    return "static const unsigned char %s[%d] = {\n    %s\n};\n" % (name, len(data), ",\n    ".join(rows))
out = ("/* Generated from assets/icon0.png by scripts/gen_appicon.py. */\n"
       "#ifndef HEARBRIDGE_APP_ICON_H\n#define HEARBRIDGE_APP_ICON_H\n\n"
       + arr("hb_icon_512", png(512)) + "\n" + arr("hb_icon_180", png(180)) + "\n#endif\n")
open(dst, "w").write(out)

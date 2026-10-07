#!/usr/bin/env python3
"""Embed src/web/index.html (with src/web/i18n.json inlined at /*I18N*/)
as a C string in src/webpage.h."""
import json, sys
src = sys.argv[1] if len(sys.argv) > 1 else "src/web/index.html"
dst = sys.argv[2] if len(sys.argv) > 2 else "src/webpage.h"
i18n = sys.argv[3] if len(sys.argv) > 3 else src.rsplit("/", 1)[0] + "/i18n.json"
html = open(src, encoding="utf-8").read()
data = json.load(open(i18n, encoding="utf-8"))
keys = set(data["strings"]["en"])
for code, d in data["strings"].items():
    missing = keys - set(d)
    if missing:
        sys.exit("i18n: %s is missing %s" % (code, sorted(missing)))
html = html.replace("/*I18N*/", json.dumps(data, ensure_ascii=False, separators=(",", ":")))
lines = []
for line in html.splitlines():
    esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?")
    lines.append('    "%s\\n"' % esc)
out = ("/* Generated from src/web/index.html + i18n.json by scripts/gen_webpage.py. */\n"
       "#ifndef HEARBRIDGE_WEBPAGE_H\n#define HEARBRIDGE_WEBPAGE_H\n\n"
       "static const char HB_WEBPAGE[] =\n" + "\n".join(lines) + ";\n\n#endif\n")
open(dst, "w", encoding="utf-8").write(out)

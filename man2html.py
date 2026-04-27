import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "./docs/issex.1"
inp = open(src, "r", encoding="utf-8").read().splitlines()

def escape_html(text):
    return (text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;"))

def inline(text):
    text = escape_html(text)
    text = text.replace("\\-", "-")
    text = text.replace("\\&", "")
    text = re.sub(r"\\fB(.*?)\\f[PR]", r"<b>\\1</b>", text)
    text = re.sub(r"\\fI(.*?)\\f[PR]", r"<i>\\1</i>", text)
    text = re.sub(r"\\f.", "", text)
    text = re.sub(r"\\\*\(?\w+\)?", "", text)
    return text.strip()

def alternating(words, tags):
    out = []
    for i, w in enumerate(words):
        w = inline(w)
        t = tags[i % len(tags)]
        if t and w:
            out.append(f"<{t}>{w}</{t}>")
        else:
            out.append(w)
    return " ".join(out)

def parse_inline_macro(line):
    m = re.match(r"\.(B|I|BR|RB|BI|IB|RI|IR)\s*(.*)", line)
    if not m:
        return None
    macro, rest = m.group(1), m.group(2)
    words = re.findall(r'"[^"]*"|\S+', rest)
    words = [w.strip('"') for w in words]
    tag_map = {
        "B":  ["b"],
        "I":  ["i"],
        "BR": ["b", ""],
        "RB": ["", "b"],
        "BI": ["b", "i"],
        "IB": ["i", "b"],
        "RI": ["", "i"],
        "IR": ["i", ""],
    }
    return alternating(words, tag_map[macro])

html = []
in_code = False
in_tp_label = False
tp_label = None
para_buf = []
in_dl = False
title = "man page"

def ensure_dl():
    global in_dl
    if not in_dl:
        html.append("<dl>")
        in_dl = True

def close_dl():
    global in_dl
    if in_dl:
        html.append("</dl>")
        in_dl = False

def flush_para():
    global para_buf, tp_label, in_tp_label
    text = " ".join(para_buf).strip()
    if tp_label is not None:
        ensure_dl()
        html.append(f"<dt>{tp_label}</dt><dd>{text}</dd>")
        tp_label = None
    else:
        if text:
            close_dl()
            html.append(f"<p>{text}</p>")
    para_buf = []
    in_tp_label = False

def flush_para_smart():
    flush_para()

i = 0
while i < len(inp):
    raw = inp[i]
    i += 1
    line = raw.strip()

    if line == ".EX":
        flush_para_smart()
        close_dl()
        html.append("<pre><code>")
        in_code = True
        continue

    if line == ".EE":
        html.append("</code></pre>")
        in_code = False
        continue

    if in_code:
        html.append(escape_html(line.replace("\\-", "-").replace("\\&", "")))
        continue

    if line.startswith('.\\"') or line == ".":
        continue

    if line == "":
        flush_para_smart()
        continue

    if line.startswith(".TH"):
        parts = line.split()
        if len(parts) >= 3:
            title = f"{parts[1]}({parts[2]})"
        continue

    if line.startswith(".SH"):
        flush_para_smart()
        close_dl()
        html.append(f"<h2>{inline(line[3:].strip().strip('\"'))}</h2>")
        continue

    if line.startswith(".SS"):
        flush_para_smart()
        close_dl()
        html.append(f"<h3>{inline(line[3:].strip().strip('\"'))}</h3>")
        continue

    if line in [".PP", ".LP", ".P"]:
        flush_para_smart()
        continue

    if line == ".RS":
        flush_para_smart()
        close_dl()
        html.append('<div class="rs">')
        continue

    if line == ".RE":
        flush_para_smart()
        close_dl()
        html.append("</div>")
        continue

    if line == ".TP":
        flush_para_smart()
        in_tp_label = True
        continue

    parsed = parse_inline_macro(line)
    if parsed is not None:
        if in_tp_label:
            ensure_dl()
            tp_label = parsed
            in_tp_label = False
        else:
            para_buf.append(parsed)
        continue

    if line.startswith("."):
        continue

    text = inline(line)
    if not text:
        continue

    if in_tp_label:
        ensure_dl()
        tp_label = text
        in_tp_label = False
    else:
        para_buf.append(text)

flush_para_smart()
close_dl()

head = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{title}</title>
<style>
body {{
  font-family: sans-serif;
  max-width: 900px;
  margin: 40px auto;
  padding: 0 20px;
  line-height: 1.6;
  color: #222;
}}
h2 {{
  border-bottom: 1px solid #ccc;
  margin-top: 36px;
  font-size: 1.2em;
  text-transform: uppercase;
}}
h3 {{
  margin-top: 20px;
  font-size: 1em;
  text-transform: uppercase;
}}
pre {{
  background: #f6f6f6;
  border: 1px solid #ddd;
  padding: 12px;
  overflow-x: auto;
}}
dl {{ margin: 0; }}
dt {{ font-family: monospace; font-weight: bold; margin-top: 12px; }}
dd {{ margin-left: 2em; }}
div.rs {{
  margin-left: 2em;
  border-left: 2px solid #e0e0e0;
  padding-left: 12px;
}}
p {{ margin: .5em 0; }}
</style>
</head>
<body>
<h1>{title}</h1>
"""

final = head + "\n".join(html) + "\n</body></html>\n"

open("docs/index.html", "w", encoding="utf-8").write(final)
print("generated index.html")

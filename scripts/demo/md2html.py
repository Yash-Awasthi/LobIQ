#!/usr/bin/env python3
"""Markdown that the research tools print (headings, paragraphs, bullets, tables, indented code) as a
standalone HTML page in the demo reports' style. Usage: md2html.py TITLE < in.md > out.html"""

import html
import re
import sys

STYLE = """:root{--bg:#fbfbfa;--fg:#1d1d1b;--muted:#6b6b66;--line:#e2e1dc;--card:#fff;--link:#2563eb;--code:#f1f0ec}
@media(prefers-color-scheme:dark){:root{--bg:#141413;--fg:#ecebe6;--muted:#9c9b95;--line:#2e2e2b;--card:#1d1d1b;--link:#60a5fa;--code:#24241f}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.55 system-ui,sans-serif}
main{max-width:1100px;margin:0 auto;padding:32px 16px 48px}h1{font-size:24px;margin:0 0 8px}h2{font-size:18px;margin:28px 0 8px}
p{margin:8px 0}a{color:var(--link)}code,pre{font:13px ui-monospace,monospace;background:var(--code);border-radius:4px}
code{padding:1px 4px}pre{padding:10px 12px;overflow-x:auto}.scroll{overflow-x:auto}
table{border-collapse:collapse;margin:8px 0;font-variant-numeric:tabular-nums}th,td{padding:5px 10px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{font-weight:600;color:var(--muted);font-size:13px}"""


def inline(s):
    s = html.escape(s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    return re.sub(r"`(.+?)`", r"<code>\1</code>", s)


def convert(src):
    out, lines, i = [], src.splitlines(), 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                if not re.fullmatch(r"\|[-| :]+\|", lines[i]):
                    rows.append([c.strip() for c in lines[i][1:-1].split("|")])
                i += 1
            head = "".join(f"<th>{inline(c)}</th>" for c in rows[0])
            body = "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in rows[1:])
            out.append(f'<div class="scroll"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
            continue
        if line.startswith("    ") and line.strip():
            block = []
            while i < len(lines) and lines[i].startswith("    "):
                block.append(lines[i][4:])
                i += 1
            out.append("<pre>" + html.escape("\n".join(block)) + "</pre>")
            continue
        m = re.match(r"(#{1,4}) (.*)", line)
        if m:
            tag = "h1" if len(m[1]) == 1 else "h2"
            out.append(f"<{tag}>{inline(m[2])}</{tag}>")
        elif line.startswith("- "):
            out.append(f"<p>&bull; {inline(line[2:])}</p>")
        elif line.strip():
            out.append(f"<p>{inline(line)}</p>")
        i += 1
    return "\n".join(out)


def main():
    title = sys.argv[1] if len(sys.argv) > 1 else "LobIQ"
    body = convert(sys.stdin.read())
    sys.stdout.write(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)}</title><style>{STYLE}</style></head>
<body><main><p><a href="index.html">Forecasting models</a> &middot; <a href="../index.html">All demo reports</a></p>
{body}
</main></body></html>
""")


if __name__ == "__main__":
    main()

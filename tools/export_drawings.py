#!/usr/bin/env python3
"""Export every inline SVG drawing in the guide to hardware/drawings/*.svg.

The guide styles its SVGs with page CSS (.sv rules + colour tokens). This script
copies those rules and the light-theme tokens into each file so the drawings
render on their own (browser, Inkscape, GitHub).

Run: python3 tools/export_drawings.py
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "etube-tap-guide.html"
OUT = ROOT / "hardware" / "drawings"


def main():
    html = SRC.read_text(encoding="utf-8")
    css = html[html.index("<style>") + 7:html.index("</style>")]

    root = re.search(r":root\s*\{(.*?)\}", css, re.S).group(1)          # light tokens
    rules = [line.replace(".sv ", "svg ") for line in css.splitlines() if line.startswith(".sv ")]
    style = ("svg { " + root.strip() + " color: var(--fg); background: var(--surface); }\n"
             + "\n".join(rules))

    OUT.mkdir(parents=True, exist_ok=True)
    count, n = 0, 0
    section = "intro"
    for m in re.finditer(r'<section id="([^"]+)"|(<svg\b[^>]*>(.*?)</svg>)', html, re.S):
        if m.group(1):
            section, n = m.group(1), 0
            continue
        svg, inner = m.group(2), m.group(3)
        if not inner.strip():          # skip the interactive demo, drawn by script
            continue
        n += 1
        open_tag = svg[:svg.index(">") + 1]
        tag = open_tag.replace("<svg", '<svg xmlns="http://www.w3.org/2000/svg"', 1)
        tag = re.sub(r'\sclass="[^"]*"', "", tag)
        doc = tag + "\n<style>\n" + style + "\n</style>\n" + svg[len(open_tag):]
        name = f"{section}-{n}.svg"
        (OUT / name).write_text(doc, encoding="utf-8")
        count += 1
        print("wrote", name)
    print(f"{count} drawings")


if __name__ == "__main__":
    main()

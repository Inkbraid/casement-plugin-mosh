#!/usr/bin/env python3
"""Draws icon.png: a terminal window with a prompt and signal arcs.

Usage: tools/make-icon.py [output.png]

Drawn at four times the size and scaled down, so the edges are smooth.
"""

import sys

from PIL import Image, ImageDraw

SIZE = 256
SCALE = 4
S = SIZE * SCALE


def px(v):
    return int(round(v * SCALE))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.png"
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Window body and title bar.
    body = (px(16), px(40), px(240), px(224))
    d.rounded_rectangle(body, radius=px(22), fill=(38, 50, 56, 255))
    d.rounded_rectangle((px(16), px(40), px(240), px(80)), radius=px(22), fill=(69, 90, 100, 255))
    d.rectangle((px(16), px(62), px(240), px(80)), fill=(69, 90, 100, 255))
    for i, colour in enumerate([(239, 83, 80), (255, 202, 40), (102, 187, 106)]):
        cx = px(42 + i * 22)
        r = px(6)
        d.ellipse((cx - r, px(60) - r, cx + r, px(60) + r), fill=colour + (255,))

    # Prompt: a chevron and a block cursor.
    green = (129, 199, 132, 255)
    w = px(14)
    d.line([(px(52), px(118)), (px(88), px(148)), (px(52), px(178))], fill=green, width=w, joint="curve")
    for x, y in [(px(52), px(118)), (px(88), px(148)), (px(52), px(178))]:
        d.ellipse((x - w // 2, y - w // 2, x + w // 2, y + w // 2), fill=green)
    d.rounded_rectangle((px(106), px(166), px(152), px(182)), radius=px(4), fill=(236, 239, 241, 255))

    # Signal arcs in the lower right corner: the session roams.
    amber = (255, 183, 77, 255)
    cx, cy = px(204), px(196)
    for r in (22, 40):
        d.arc((cx - px(r), cy - px(r), cx + px(r), cy + px(r)), start=225, end=315, fill=amber, width=px(9))
    d.ellipse((cx - px(7), cy - px(7), cx + px(7), cy + px(7)), fill=amber)

    img.resize((SIZE, SIZE), Image.LANCZOS).save(out, optimize=True)


if __name__ == "__main__":
    main()

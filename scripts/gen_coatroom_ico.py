#!/usr/bin/env python3
"""Generate Coatroom multi-resolution RGBA PNG-in-ICO branding asset.

Creates resources/coatroom.ico containing 16, 24, 32, 48, 64, 128, and 256 px
RGBA PNG frames matching Coatroom's Adobe Creative Cloud-inspired squircle badge:
  - Superellipse equation: |2x - 1|^4.4 + |2y - 1|^4.4 <= 1.0
  - Dark midnight emerald-teal vertical gradient: #052228 -> #041B20
  - Crisp geometric bold "Cr" in #2FE6CE using C:/Windows/Fonts/segoeuib.ttf
  - Zero outer glow, zero corner brackets

Usage:
  python scripts/gen_coatroom_ico.py
  python scripts/gen_coatroom_ico.py --out resources/coatroom.ico --preview build/smoke_test/coatroom_icon_256.png
"""

from __future__ import annotations

import argparse
import io
import math
import os
import struct
from pathlib import Path
from typing import Iterable, List, Tuple

from PIL import Image, ImageDraw, ImageFont

ICO_SIZES: Tuple[int, ...] = (16, 24, 32, 48, 64, 128, 256)
SQUIRCLE_EXPONENT = 4.4
TOP_COLOR = (0x05, 0x22, 0x28)       # #052228
BOTTOM_COLOR = (0x04, 0x1B, 0x20)    # #041B20
TEXT_COLOR = (0x2F, 0xE6, 0xCE, 255) # #2FE6CE
DEFAULT_FONT_PATH = Path("C:/Windows/Fonts/segoeuib.ttf")


def _squircle_polygon(size: int, margin_frac: float = 0.02, steps: int = 720) -> List[Tuple[float, float]]:
    """Sample the superellipse |2x - 1|^4.4 + |2y - 1|^4.4 = 1.0 in pixel space."""
    cx = (size - 1) * 0.5
    cy = (size - 1) * 0.5
    rx = size * (0.5 - margin_frac)
    ry = size * (0.5 - margin_frac)
    inv_exp = 2.0 / SQUIRCLE_EXPONENT

    pts: List[Tuple[float, float]] = []
    for i in range(steps):
        theta = (2.0 * math.pi * i) / steps
        c = math.cos(theta)
        s = math.sin(theta)
        u = math.copysign(abs(c) ** inv_exp, c)
        v = math.copysign(abs(s) ** inv_exp, s)
        pts.append((cx + rx * u, cy + ry * v))
    return pts


def _make_vertical_gradient(size: int) -> Image.Image:
    """Create an RGB image with a smooth top-to-bottom #052228 -> #041B20 gradient."""
    grad = Image.new("RGB", (1, size))
    px = grad.load()
    denom = max(1, size - 1)
    for y in range(size):
        t = y / denom
        r = int(round(TOP_COLOR[0] * (1.0 - t) + BOTTOM_COLOR[0] * t))
        g = int(round(TOP_COLOR[1] * (1.0 - t) + BOTTOM_COLOR[1] * t))
        b = int(round(TOP_COLOR[2] * (1.0 - t) + BOTTOM_COLOR[2] * t))
        px[0, y] = (r, g, b)
    return grad.resize((size, size), Image.Resampling.BILINEAR)


def render_coatroom_icon(target_size: int, font_path: Path = DEFAULT_FONT_PATH) -> Image.Image:
    """Render a single anti-aliased RGBA Coatroom squircle icon at target_size."""
    ss = 4 if target_size <= 128 else 2
    hi = target_size * ss

    # Build supersampled squircle alpha mask (zero outer glow, crisp AA edge)
    mask = Image.new("L", (hi, hi), 0)
    mask_draw = ImageDraw.Draw(mask)
    poly = _squircle_polygon(hi, margin_frac=0.025)
    mask_draw.polygon(poly, fill=255)

    # Fill squircle with midnight emerald-teal gradient
    bg_rgb = _make_vertical_gradient(hi).convert("RGBA")
    canvas = Image.new("RGBA", (hi, hi), (0, 0, 0, 0))
    canvas.paste(bg_rgb, (0, 0), mask)

    # Render geometric bold "Cr" centered inside the squircle
    if not font_path.exists():
        raise FileNotFoundError(f"Required font not found: {font_path}")

    font_px = max(8, int(round(hi * 0.56)))
    font = ImageFont.truetype(str(font_path), size=font_px)

    text_layer = Image.new("RGBA", (hi, hi), (0, 0, 0, 0))
    text_draw = ImageDraw.Draw(text_layer)

    # Measure tight glyph bounding box for true optical centering
    bbox = text_draw.textbbox((0, 0), "Cr", font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = (hi - tw) * 0.5 - bbox[0]
    ty = (hi - th) * 0.5 - bbox[1] - hi * 0.015
    text_draw.text((tx, ty), "Cr", font=font, fill=TEXT_COLOR)

    # Clip text strictly to squircle interior
    text_clipped = Image.new("RGBA", (hi, hi), (0, 0, 0, 0))
    text_clipped.paste(text_layer, (0, 0), mask)
    canvas = Image.alpha_composite(canvas, text_clipped)

    if ss > 1:
        canvas = canvas.resize((target_size, target_size), Image.Resampling.LANCZOS)
    return canvas


def pack_png_ico(images: Iterable[Image.Image]) -> bytes:
    """Pack RGBA images as standard Windows PNG-in-ICO entries."""
    frames: List[Tuple[int, int, bytes]] = []
    for img in images:
        rgba = img.convert("RGBA")
        buf = io.BytesIO()
        rgba.save(buf, format="PNG", optimize=True)
        frames.append((rgba.width, rgba.height, buf.getvalue()))

    count = len(frames)
    header = struct.pack("<HHH", 0, 1, count)
    offset = 6 + 16 * count

    entries = bytearray()
    payload = bytearray()
    for w, h, png_bytes in frames:
        wb = 0 if w >= 256 else w
        hb = 0 if h >= 256 else h
        entries.extend(
            struct.pack(
                "<BBBBHHII",
                wb,
                hb,
                0,   # color count (0 for >=8bpp)
                0,   # reserved
                1,   # color planes
                32,  # bits per pixel
                len(png_bytes),
                offset,
            )
        )
        payload.extend(png_bytes)
        offset += len(png_bytes)

    return header + bytes(entries) + bytes(payload)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Generate Coatroom multi-res PNG-in-ICO icon.")
    parser.add_argument(
        "--out",
        type=Path,
        default=repo_root / "resources" / "coatroom.ico",
        help="Output .ico file path (default: resources/coatroom.ico)",
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=None,
        help="Optional 256x256 verification PNG output path",
    )
    parser.add_argument(
        "--font",
        type=Path,
        default=DEFAULT_FONT_PATH,
        help="TTF font path for 'Cr' monogram (default: C:/Windows/Fonts/segoeuib.ttf)",
    )
    args = parser.parse_args()

    rendered = [render_coatroom_icon(sz, font_path=args.font) for sz in ICO_SIZES]
    ico_bytes = pack_png_ico(rendered)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(ico_bytes)
    print(f"[OK] Wrote {args.out} ({len(ico_bytes)} bytes, sizes={list(ICO_SIZES)}, PNG-in-ICO)")

    if args.preview is not None:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        rendered[-1].save(args.preview, format="PNG")
        print(f"[OK] Wrote preview PNG {args.preview} ({rendered[-1].width}x{rendered[-1].height})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

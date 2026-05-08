#!/usr/bin/env python3
"""Generate a single images.h header from a folder of PNGs.

The script scans a folder, converts every PNG into a 1-bit byte array, and
writes a combined C/C++ header in the same style as src/modules/images.h.
"""

import argparse
import re
import sys
from pathlib import Path

from PIL import Image


TARGET_WIDTH = 296
TARGET_HEIGHT = 128
SIZE_SUFFIX = re.compile(r"_(\d+)x(\d+)$")

# Current project naming does not map 1:1 to file names, so keep a small alias
# table for the generated header symbols.
ALIAS_MAP = {
    "icon_battery_charging": "status_battery_charging",
    "icon_battery_discharging": "status_battery_discharging",
    "icon_sleeping": "status_sleeping",
    "icon_tv_16x16": "icon_tv",
    "icon_tv_20x20": "icon_tv",
}

ICON_ORDER = [
    "icon_bulb",
    "icon_monitor",
    "icon_tv",
    "icon_unmute",
    "icon_flame",
]

TILE_ORDER = [
    "tile_bulb",
    "tile_socket",
    "tile_flame",
    "tile_tv",
    "tile_monitor",
    "tile_unmute",
    "tile_mute",
    "tile_power",
    "tile_input",
    "tile_aspect",
    "tile_minus",
    "tile_plus",
    "tile_phone",
    "tile_boiler",
    "tile_radiator",
]

CATEGORY_ORDER = ["status", "indicator", "tile", "icon"]


parser = argparse.ArgumentParser(description="Generate images.h from a folder of PNGs.")
parser.add_argument("folder", type=Path, help="folder containing PNG images")
parser.add_argument("--out", type=Path, default=None, help="output header path (defaults to <folder>/images.h)")
parser.add_argument("--resize", action="store_true", help="force images to 296x128 before conversion")
options = parser.parse_args()


def normalize_symbol(stem: str) -> str:
    stem = SIZE_SUFFIX.sub("", stem)
    return ALIAS_MAP.get(stem, stem)


def convert_image(path: Path):
    with Image.open(path) as img:
        if options.resize:
            img = img.resize((TARGET_WIDTH, TARGET_HEIGHT))
        img = img.convert("L").point(lambda x: 255 if x > 200 else 0, mode="1")
        return img


def image_bytes(img: Image.Image) -> list[int]:
    return [~b & 0xFF for b in img.tobytes()]


def category_of(symbol: str) -> str:
    return symbol.split("_", 1)[0]


def emit_array(symbol: str, data: list[int]) -> str:
    byte_data = ", ".join(f"0x{b:02x}" for b in data)
    return (
        f"static const char image_{symbol}[{len(data)}] = {{\n"
        f"    {byte_data}\n"
        f"}};\n"
    )


def emit_pointer_array(name: str, symbols: list[str]) -> str:
    if not symbols:
        return ""
    entries = ", ".join(f"image_{symbol}" for symbol in symbols)
    comments = "\n".join(f"// {idx}: image_{symbol}" for idx, symbol in enumerate(symbols))
    return f"{comments}\nconst char* const image_{name}[] = {{{entries}}};\n"


def select_assets(folder: Path):
    assets = {}
    for path in sorted(folder.rglob("*.png")):
        symbol = normalize_symbol(path.stem)
        img = convert_image(path)
        w, h = img.size
        data = image_bytes(img)
        area = w * h

        current = assets.get(symbol)
        if current is None or area < current["area"]:
            assets[symbol] = {
                "path": path,
                "size": (w, h),
                "area": area,
                "data": data,
            }
    return assets


def group_size(assets, prefix: str):
    for symbol in assets:
        if symbol.startswith(f"{prefix}_"):
            return assets[symbol]["size"]
    return None


def main():
    folder = options.folder
    if not folder.is_dir():
        raise SystemExit(f"{folder} is not a folder")

    assets = select_assets(folder)

    ordered_symbols = []
    seen = set()
    for category in CATEGORY_ORDER:
        for symbol in sorted(assets):
            if symbol in seen or category_of(symbol) != category:
                continue
            ordered_symbols.append(symbol)
            seen.add(symbol)
    for symbol in sorted(assets):
        if symbol not in seen:
            ordered_symbols.append(symbol)

    lines = ["#pragma once", ""]

    for prefix in ("status", "indicator", "tile"):
        size = group_size(assets, prefix)
        if size is not None:
            lines.append(f"static const char image_{prefix}_size = {size[0]};")

    if any(line.startswith("static const char image_") for line in lines[2:]):
        lines.append("")

    for symbol in ordered_symbols:
        lines.append(emit_array(symbol, assets[symbol]["data"]).rstrip())
        lines.append("")

    icon_names = [name for name in ICON_ORDER if name in assets]
    icon_array = emit_pointer_array("icons", icon_names)
    if icon_array:
        lines.append(icon_array.rstrip())
        lines.append("")

    tile_names = [name for name in TILE_ORDER if name in assets]
    tile_array = emit_pointer_array("tiles", tile_names)
    if tile_array:
        lines.append(tile_array.rstrip())
        lines.append("")

    output = "\n".join(lines).rstrip() + "\n"

    if options.out is None:
        sys.stdout.write(output)
    else:
        with options.out.open("w") as out:
            out.write(output)


if __name__ == "__main__":
    main()

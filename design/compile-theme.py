#!/usr/bin/env python3
"""Validate the current native theme contract and compile its C constants."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent

def compile_theme(path):
    data = json.loads(path.read_text())
    if data.get("version") != 2:
        raise ValueError("Unsupported theme version")
    def color(value):
        if not isinstance(value, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
            raise ValueError("Theme colors must use six-digit RGB notation")
        return "0x" + value[1:].lower()
    def integer(value, low, high):
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f"Theme integer must be in {low}..{high}")
        return value
    def keys(obj, names):
        if not isinstance(obj, dict) or set(obj) != set(names):
            raise ValueError(f"Expected fields: {', '.join(names)}")
    def boolean(v):
        if type(v) is not bool: raise ValueError("Expected a theme boolean")
        return int(v)
    def choice(v, names, prefix):
        if v not in names: raise ValueError(f"Expected one of {names}")
        return prefix + v.upper()
    def edges(v, limit):
        keys(v, ["top", "right", "bottom", "left"])
        return "{" + ", ".join(f".{k} = {integer(v[k], 0, limit)}" for k in v) + "}"
    fields = {
        "colors": ["surface", "text", "muted", "accent", "panel", "boundary", "status_text"],
        "font_scale": ["title", "page", "menu", "body", "hint"]
    }
    keys(data, ["version", "name", "colors", "font_scale", "status", "icon_palette"])
    for name, names in fields.items(): keys(data[name], names)
    status = data["status"]
    keys(status, ["box", "layout", "icon_height", "text_height", "text_gap", "transition_ms",
                  "transition_easing", "visible", "animations", "collapse_hidden"] +
                  (["pixel_scale"] if "pixel_scale" in status else []) +
                  (["indicator_order"] if "indicator_order" in status else []))
    pixel_scale = integer(status.get("pixel_scale", 1), 1, 2)
    order = status.get("indicator_order", [])
    if (not isinstance(order, list) or len(order) > 8 or
        any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", name) for name in order) or
        len(set(order)) != len(order)):
        raise ValueError("Indicator order must contain at most eight unique native indicator IDs")
    if status["icon_height"] not in [20, 30] or type(status["icon_height"]) is not int:
        raise ValueError("Native status artwork supports 20px or 30px icon height")
    integer(status["text_height"], 1, status["icon_height"])
    integer(status["text_gap"], 0, 64)
    integer(status["transition_ms"], 0, 2000)
    box = status["box"]
    box_fields = ["background", "background_alpha", "border_color", "border_alpha", "opacity", "border", "radius",
               "border_style", "shadow", "blend_mode", "blur", "backdrop_blur", "clip_children"]
    keys(box, box_fields + (["frame"] if "frame" in box else []))
    bvalues = {k: color(box[k]) for k in ["background", "border_color"]}
    bvalues.update({k: integer(box[k], 0, 255) for k in ["background_alpha", "border_alpha", "opacity"]})
    bvalues["border"] = edges(box["border"], 64)
    keys(box["radius"], ["top_left", "top_right", "bottom_right", "bottom_left"])
    bvalues["radius"] = "{" + ", ".join(f".{k} = {integer(v, 0, 256)}" for k, v in box["radius"].items()) + "}"
    bvalues["border_style"] = choice(box["border_style"], ["solid", "dashed", "dotted"], "MP_BORDER_")
    frame = box.get("frame", {"art": "none", **dict.fromkeys(["highlight", "light", "mid", "face", "shadow", "recess"], "#000000")})
    keys(frame, ["art", "highlight", "light", "mid", "face", "shadow", "recess"])
    fvalues = {"art": choice(frame["art"], ["none", "moonstone"], "MP_FRAME_")}
    fvalues.update({k: color(frame[k]) for k in ["highlight", "light", "mid", "face", "shadow", "recess"]})
    bvalues["frame"] = "{" + ", ".join(f".{k} = {v}" for k, v in fvalues.items()) + "}"
    if frame["art"] == "moonstone" and (box["border"]["top"] or box["border"]["right"] or
        box["border"]["left"] != box["border"]["bottom"] or not 6 <= box["border"]["left"] <= 32 or
        box["border_style"] != "solid" or any(box["radius"].values())):
        raise ValueError("Moonstone requires equal 6..32px left/bottom borders, open top/right, solid edges and no radii")
    shadow = box["shadow"]
    keys(shadow, ["color", "alpha", "blur", "x", "y", "spread"])
    bvalues["shadow"] = "{" + ", ".join(f".{k} = {v}" for k, v in {
        "color": color(shadow["color"]), "alpha": integer(shadow["alpha"], 0, 255), "blur": integer(shadow["blur"], 0, 8),
        "x": integer(shadow["x"], -128, 128), "y": integer(shadow["y"], -128, 128),
        "spread": integer(shadow["spread"], -32, 32)}.items()) + "}"
    bvalues["blend_mode"] = choice(box["blend_mode"], ["normal", "multiply", "screen", "overlay", "darken", "lighten",
        "difference", "exclusion", "add"], "MP_BLEND_")
    for k in ["blur", "backdrop_blur"]: bvalues[k] = integer(box[k], 0, 8)
    bvalues["clip_children"] = boolean(box["clip_children"])
    layout = status["layout"]
    keys(layout, ["orientation", "direction", "justify", "align", "anchor_x", "anchor_y", "padding", "margin", "gap",
                  "width", "height", "min_width", "min_height", "max_width", "max_height", "offset_x", "offset_y", "overflow"])
    lvalues = {
        "orientation": choice(layout["orientation"], ["horizontal", "vertical"], "MP_"),
        "direction": choice(layout["direction"], ["forward", "reverse"], "MP_"),
        "justify": choice(layout["justify"], ["start", "center", "end"], "MP_JUSTIFY_") if layout["justify"] in ["start", "center", "end"] else choice(layout["justify"], ["between", "around", "evenly"], "MP_SPACE_"),
        "align": choice(layout["align"], ["start", "center", "end", "stretch"], "MP_ALIGN_"),
        "anchor_x": choice(layout["anchor_x"], ["start", "center", "end"], "MP_ALIGN_"),
        "anchor_y": choice(layout["anchor_y"], ["start", "center", "end"], "MP_ALIGN_"),
        "padding": edges(layout["padding"], 128), "margin": edges(layout["margin"], 4096),
        "gap": integer(layout["gap"], 0, 64), "overflow": choice(layout["overflow"], ["reject", "clip"], "MP_OVERFLOW_")}
    for k in ["width", "height", "min_width", "min_height", "max_width", "max_height"]:
        lvalues[k] = integer(layout[k], 1 if k.startswith("max_") else 0, 512)
    for k in ["offset_x", "offset_y"]: lvalues[k] = integer(layout[k], -4096, 4096)
    for axis in ["width", "height"]:
        if layout["min_" + axis] > layout["max_" + axis] or (layout[axis] and not layout["min_" + axis] <= layout[axis] <= layout["max_" + axis]):
            raise ValueError("Inconsistent layout size bounds")
    if any(layout["padding"][edge] + box["border"][edge] > 128 for edge in ["top", "right", "bottom", "left"]):
        raise ValueError("Combined border and padding must not exceed 128px per edge")
    # Providers determine intrinsic dimensions. The registry checks their total
    # size against layout, viewport, and the bounded raster cache at registration.
    palette = data["icon_palette"]
    if len(palette) != 13: raise ValueError("Icon palette must contain 13 entries")
    lines = ['/* SPDX-License-Identifier: GPL-2.0-only */',
             '/* Generated from design/themes/minpocket-default/theme.json. */',
             '#include "theme.h"', 'const struct mp_theme mp_theme_default = {']
    for name, keys in fields.items():
        values = []
        for key in keys:
            value = data[name][key]
            if name == "colors": value = color(value)
            elif name == "font_scale": value = integer(value, 1, 8)
            values.append(f".{key} = {value}")
        lines.append(f"    .{name} = {{" + ", ".join(values) + "},")
    def initializer(values):
        return "{" + ", ".join(f".{k} = {v}" for k, v in values.items()) + "}"
    svalues = {"box": initializer(bvalues), "layout": initializer(lvalues)}
    svalues["pixel_scale"] = pixel_scale
    svalues["indicator_order"] = "{" + (", ".join('"' + name + '"' for name in order) if order else "0") + "}"
    svalues["indicator_order_count"] = len(order)
    for k in ["icon_height", "text_height", "text_gap", "transition_ms"]: svalues[k] = status[k]
    svalues["transition_easing"] = choice(status["transition_easing"], ["out_cubic", "linear", "in_out_cubic"], "MP_EASE_")
    for k in ["visible", "animations", "collapse_hidden"]: svalues[k] = boolean(status[k])
    lines.append("    .status = " + initializer(svalues) + ",")
    lines.append("    .icon_palette = {" + ", ".join(map(color, palette)) + "}")
    lines.append("};\n")
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / "design/themes/minpocket-default/theme.json")
    parser.add_argument("--output", type=Path, default=ROOT / "native/theme-default.c")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try: compiled = compile_theme(args.input)
    except (ValueError, KeyError, TypeError) as error: parser.error(str(error))
    if args.check:
        if not args.output.exists() or args.output.read_text() != compiled:
            parser.error("Compiled theme differs from its JSON source")
    else: args.output.write_text(compiled)

if __name__ == "__main__": main()

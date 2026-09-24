#!/usr/bin/env python3
"""M5 previews from the CPU reference (docs/PLAN.md M5): upscalers and palettes at on-screen size.

Runs `harness render` for benchmark frames at the view-size presets and writes lossless PNGs:
- kernels_full.png: one crop per scene at 1:1 screen pixels, one row per kernel (nearest is what the
  app shows today);
- palettes.png: each palette's gradient strip, then a scene in each palette;
- kernels_phone.jpg (quality 95, no chroma subsampling: the whole frame at the Phone preset);
- build/m5/full_keyboard_<kernel>.png: whole frames at the Full preset, for viewing on the tablet itself
  (local only).

    tools/py/.venv/bin/python tools/py/m5_preview.py --out bench/results/<label>/m5_preview \\
        [--pipeline detail] [--frame 100]
"""

import argparse
import pathlib
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "build" / "harness" / "harness"
PRESETS = {"full": (2133, 1600), "phone": (880, 660)}  # PLAN M6 starting values (10.9" and ~4.5")
KERNELS = [("nearest", False, "nearest (the app today)"), ("catmullrom", True, "Catmull-Rom + 2x2 clamp"),
           ("lanczos3", True, "Lanczos-3 + 2x2 clamp"), ("bspline", True, "cardinal B-spline + 2x2 clamp")]
# Crops in camera pixels: what each scene's interesting part is (keys, the car, the room's shelf).
CROPS = {"keyboard": (60, 100, 68, 51), "night": (70, 50, 68, 51), "room": (20, 50, 68, 51)}


def render(scene, out, size, kernel, clamp, pipeline, frame, palette="white_hot"):
    cmd = [HARNESS, "render", ROOT / "bench" / scene / "thermalview", "--frame", str(frame), "--size",
           f"{size[0]}x{size[1]}", "--pipeline", pipeline, "--kernel", kernel,
           "--palette", ROOT / "palettes" / f"{palette}.json", "--out", out]
    if clamp:
        cmd.append("--clamp")
    subprocess.run(cmd, check=True)
    return Image.open(out).convert("RGB")


def label(img, text):
    font = ImageFont.load_default(size=20)
    draw = ImageDraw.Draw(img)
    box = draw.textbbox((8, 6), text, font=font)
    draw.rectangle((box[0] - 4, box[1] - 3, box[2] + 4, box[3] + 3), fill=(0, 0, 0))
    draw.text((8, 6), text, fill=(255, 230, 0), font=font)
    return img


def grid(rows, gap=6):
    w = sum(t.width for t in rows[0]) + gap * (len(rows[0]) - 1)
    h = sum(r[0].height for r in rows) + gap * (len(rows) - 1)
    sheet = Image.new("RGB", (w, h), (40, 40, 40))
    y = 0
    for r in rows:
        x = 0
        for t in r:
            sheet.paste(t, (x, y))
            x += t.width + gap
        y += r[0].height + gap
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--pipeline", default="default")
    ap.add_argument("--frame", type=int, default=100)
    args = ap.parse_args()
    subprocess.run(["cmake", "--build", ROOT / "build" / "harness"], check=True, stdout=subprocess.DEVNULL)
    args.out.mkdir(parents=True, exist_ok=True)
    tmp = ROOT / "build" / "m5"
    tmp.mkdir(parents=True, exist_ok=True)
    for preset, size in PRESETS.items():
        sx, sy = size[0] / 256.0, size[1] / 192.0
        rows = []
        for kernel, clamp, name in KERNELS:
            row = []
            for scene, (x, y, w, h) in CROPS.items():
                img = render(scene, tmp / f"{scene}_{preset}_{kernel}.ppm", size, kernel, clamp, args.pipeline, args.frame)
                box = (int(x * sx), int(y * sy), int((x + w) * sx), int((y + h) * sy))
                crop = img.crop(box) if preset == "full" else img
                row.append(label(crop.copy(), f"{scene}, {preset} {size[0]}x{size[1]}: {name}"))
                if preset == "full" and scene == "keyboard":
                    img.save(tmp / f"full_{scene}_{kernel}.png")
            rows.append(row)
        if preset == "full":
            grid(rows).save(args.out / "kernels_full.png")
        else:
            grid(rows).save(args.out / f"kernels_{preset}.jpg", quality=95, subsampling=0)
        print(args.out / f"kernels_{preset}")
    strips, scenes = [], []
    for palette in ("white_hot", "rainbow_hc"):
        subprocess.run([HARNESS, "palette", ROOT / "palettes" / f"{palette}.json", "--out", tmp / f"{palette}.ppm"], check=True)
        strips.append(label(Image.open(tmp / f"{palette}.ppm").convert("RGB").resize((880, 64)), palette))
        scenes.append(label(render("room", tmp / f"room_{palette}.ppm", PRESETS["phone"], "bspline", True, args.pipeline,
                                   args.frame, palette), f"room, {palette}, B-spline"))
    grid([strips, scenes]).save(args.out / "palettes.png")
    print(args.out / "palettes.png")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Pipeline configurations side by side, for a stage's visual review (docs/PLAN.md M4).

bench.py compares the pipeline against the M3 baseline; a new stage is judged against the stages
already approved. This runs `harness bench` once per configuration (into build/compare/), then
writes, per scene, one frame from each side by side at 4x (bicubic, as the benchmark sheets), plus
optional crops at 8x, labelled with the stage text (or --name):

    tools/py/.venv/bin/python tools/py/compare_stages.py --config default --config detail \\
        [--name "stages 1-5" --name "stage 6"] --out bench/results/<label>/stage6_vs_5 \\
        [--frame 100] [--crop keyboard:16,96,184,64] [SCENE ...]
"""

import argparse
import pathlib
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS_BUILD = ROOT / "build" / "harness"
W, H = 256, 192


def run(stages, out, scenes):
    subprocess.run(["cmake", "--build", HARNESS_BUILD], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([HARNESS_BUILD / "harness", "bench", "--out", out, "--pipeline", stages, *scenes], check=True,
                   stdout=subprocess.DEVNULL)


def frame(out, scene, index):
    d = pathlib.Path(out) / scene
    x = np.fromfile(d / "pipeline.f32", dtype="<f4").reshape(-1, H, W)
    return x[min(index, len(x) - 1)]


def render(img, scale, box=None):
    if box:
        x, y, w, h = box
        img = img[y:y + h, x:x + w]
    h, w = img.shape
    big = np.asarray(Image.fromarray(img.astype(np.float32)).resize((w * scale, h * scale), Image.BICUBIC))
    return Image.fromarray(np.clip(np.rint(big * 255.0), 0, 255).astype(np.uint8)).convert("RGB")


def labelled(tiles, labels):
    font = ImageFont.load_default(size=22)
    width = sum(t.width for t in tiles) + 8 * (len(tiles) - 1)
    sheet = Image.new("RGB", (width, max(t.height for t in tiles)), (40, 40, 40))
    x = 0
    for tile, text in zip(tiles, labels):
        sheet.paste(tile, (x, 0))
        draw = ImageDraw.Draw(sheet)
        box = draw.textbbox((x + 6, 4), text, font=font)
        draw.rectangle((box[0] - 4, box[1] - 3, box[2] + 4, box[3] + 3), fill=(0, 0, 0))
        draw.text((x + 6, 4), text, fill=(255, 230, 0), font=font)
        x += tile.width + 8
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--config", action="append", required=True, help="stage text, once per column (e.g. default)")
    ap.add_argument("--name", action="append", default=[], help="column labels, in --config order")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--frame", type=int, default=100)
    ap.add_argument("--crop", action="append", default=[], help="scene:x,y,w,h, drawn at 8x as well")
    ap.add_argument("scenes", nargs="*", default=["keyboard", "room", "night", "hand", "flat_aged"])
    args = ap.parse_args()
    crops = {}
    for c in args.crop:
        scene, box = c.split(":")
        crops.setdefault(scene, []).append(tuple(int(v) for v in box.split(",")))
    work = ROOT / "build" / "compare"
    for k, stages in enumerate(args.config):
        run(stages, work / str(k), args.scenes)
    names = args.name + args.config[len(args.name):]
    args.out.mkdir(parents=True, exist_ok=True)
    for scene in args.scenes:
        frames = [frame(work / str(k), scene, args.frame) for k in range(len(args.config))]
        labels = [f"{scene}: {n}" for n in names]
        labelled([render(f, 4) for f in frames], labels).save(args.out / f"{scene}.jpg", quality=92)
        for k, box in enumerate(crops.get(scene, [])):
            labelled([render(f, 8, box) for f in frames], labels).save(args.out / f"{scene}_crop{k}.jpg", quality=92)
        print(f"{scene}: {args.out / (scene + '.jpg')}")


if __name__ == "__main__":
    main()

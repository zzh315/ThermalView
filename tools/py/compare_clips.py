#!/usr/bin/env python3
"""Pipeline configurations side by side as clips, for temporal effects (CLAUDE.md rule 4: clips for
temporal effects; docs/PLAN.md M4). Stills can't show what changes from frame to frame (the stripes'
flicker, noise crawl): this runs `harness bench` once per configuration and writes, per scene, an
H.264 clip of every frame at 4x (bicubic, as the benchmark sheets), the configurations side by side,
encoded nearly losslessly (crf 12) so the compression doesn't smooth away what is compared:

    tools/py/.venv/bin/python tools/py/compare_clips.py --config default --config default,stripes=1 \\
        [--name "stage 3c off" --name "stage 3c on"] [--crop scene:x,y,w,h --scale 8] \\
        --out bench/out/clips/stage3c [SCENE ...]
"""

import argparse
import pathlib
import subprocess

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS_BUILD = ROOT / "build" / "harness"
W, H = 256, 192


def run(stages, out, scenes):
    subprocess.run(["cmake", "--build", HARNESS_BUILD], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([HARNESS_BUILD / "harness", "bench", "--out", out, "--pipeline", stages, *scenes], check=True,
                   stdout=subprocess.DEVNULL)


def frames(out, scene):
    return np.fromfile(pathlib.Path(out) / scene / "pipeline.f32", dtype="<f4").reshape(-1, H, W)


def font(size):
    for f in ("/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"):
        if pathlib.Path(f).exists():
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--config", action="append", required=True, help="stage text, once per panel")
    ap.add_argument("--name", action="append", default=[], help="panel labels, in --config order")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--crop", action="append", default=[], help="scene:x,y,w,h: that part only (camera pixels)")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("scenes", nargs="*", default=["flat_aged", "room", "keyboard", "night"])
    args = ap.parse_args()
    names = args.name + args.config[len(args.name):]
    crops = {c.split(":")[0]: tuple(int(v) for v in c.split(":")[1].split(",")) for c in args.crop}
    args.out.mkdir(parents=True, exist_ok=True)
    work = ROOT / "build" / "compare_clips"
    outs = []
    for k, stages in enumerate(args.config):
        out = work / f"c{k}"
        run(stages, out, args.scenes)
        outs.append(out)
    f = font(22)
    for scene in args.scenes:
        seqs = [frames(o, scene) for o in outs]
        n = min(len(s) for s in seqs)
        x0, y0, cw, ch = crops.get(scene, (0, 0, W, H))
        pw, ph = cw * args.scale, ch * args.scale
        total_w = len(seqs) * pw + 8 * (len(seqs) - 1)
        label = Image.new("RGB", (total_w, ph), (0, 0, 0))
        d = ImageDraw.Draw(label)
        mask = Image.new("L", (total_w, ph), 0)
        dm = ImageDraw.Draw(mask)
        for k, text in enumerate(names):
            box = d.textbbox((0, 0), text, font=f)
            rect = (k * (pw + 8), 0, k * (pw + 8) + box[2] + 16, box[3] + 16)
            d.rectangle(rect, fill=(0, 0, 0))
            d.text((k * (pw + 8) + 8, 8), text, fill=(255, 255, 0), font=f)
            dm.rectangle(rect, fill=255)
        label_np, mask_np = np.asarray(label), np.asarray(mask)[..., None] > 0
        clip = args.out / f"{scene}.mp4"
        proc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                                 f"{total_w}x{ph}", "-r", "25", "-i", "-", "-c:v", "libx264", "-crf", "12",
                                 "-preset", "slow", "-pix_fmt", "yuv420p", str(clip)], stdin=subprocess.PIPE)
        for t in range(n):
            canvas = np.zeros((ph, total_w, 3), np.uint8)
            for k, s in enumerate(seqs):
                img = np.clip(s[t, y0:y0 + ch, x0:x0 + cw], 0, 1)
                up = cv2.resize(img, (pw, ph), interpolation=cv2.INTER_CUBIC)
                canvas[:, k * (pw + 8):k * (pw + 8) + pw] = (np.clip(up, 0, 1) * 255 + 0.5).astype(np.uint8)[..., None]
            canvas = np.where(mask_np, label_np, canvas)
            proc.stdin.write(canvas.tobytes())
        proc.stdin.close()
        proc.wait()
        print(f"{scene}: {clip}")


if __name__ == "__main__":
    main()

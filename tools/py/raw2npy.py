#!/usr/bin/env python3
"""Convert a ThermalView dump to per-frame .npy files (docs/PLAN.md M1).

Each output is a (196, 256) uint16 array — the full frame, metadata rows included — which is
what ht301_hacklib's CameraEmulator loads, one frame per file.

    tools/py/.venv/bin/python tools/py/raw2npy.py DUMP [OUT_DIR]

DUMP is the dump path with or without .raw; OUT_DIR defaults to <DUMP>_npy next to it.
"""

import argparse
import json
import pathlib
import sys

import numpy as np

WIDTH, HEIGHT = 256, 196


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("out", nargs="?")
    args = ap.parse_args()

    base = pathlib.Path(args.dump)
    if base.suffix in (".raw", ".json"):
        base = base.with_suffix("")
    raw_path, json_path = base.with_suffix(".raw"), base.with_suffix(".json")
    raw = np.fromfile(raw_path, dtype="<u2")
    if raw.size == 0 or raw.size % (WIDTH * HEIGHT):
        sys.exit(f"{raw_path}: not a whole number of {WIDTH}x{HEIGHT} frames")
    if json_path.exists():
        meta = json.loads(json_path.read_text())
        if (meta.get("width"), meta.get("height")) != (WIDTH, HEIGHT):
            sys.exit(f"{json_path}: unexpected geometry {meta.get('width')}x{meta.get('height')}")
    frames = raw.reshape(-1, HEIGHT, WIDTH)

    out = pathlib.Path(args.out) if args.out else base.parent / f"{base.name}_npy"
    out.mkdir(parents=True, exist_ok=True)
    for i, frame in enumerate(frames):
        np.save(out / f"{base.name}_{i:04d}.npy", frame)
    print(f"{len(frames)} frames -> {out}")


if __name__ == "__main__":
    main()

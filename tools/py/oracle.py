#!/usr/bin/env python3
"""Temperature oracle (docs/PLAN.md M2): ht301_hacklib's CameraEmulator (vendored, GPL-3.0) on
frames of a ThermalView dump. Writes golden files for native/core's tests, or prints readings.

    tools/py/.venv/bin/python tools/py/oracle.py DUMP --frames 0 --golden native/core/tests/golden/NAME
    tools/py/.venv/bin/python tools/py/oracle.py DUMP --frames 0,100 --at 38,108 --at 119,108

Golden output per frame: NAME_fNNNN.meta (the 4 metadata rows, 1024 little-endian uint16) and
NAME_fNNNN.lut (16384 little-endian float32, normal range). With --high, also NAME_fNNNN.hi_ht301
(ht301_hacklib's high-range scaling, 1.17 x T - 40.9 on the same table).

Only CameraEmulator is ever constructed: Camera() would open an attached camera and send commands
outside our gate (CLAUDE.md rule 1).
"""

import argparse
import json
import pathlib
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "vendor"))
import ht301_hacklib  # noqa: E402  (vendored at 2f1498d)

WIDTH, HEIGHT, ROWS = 256, 196, 192


def load_frames(dump):
    base = pathlib.Path(dump)
    if base.suffix in (".raw", ".json"):
        base = base.with_suffix("")
    frames = np.fromfile(base.with_suffix(".raw"), dtype="<u2").reshape(-1, HEIGHT, WIDTH)
    meta = json.loads(base.with_suffix(".json").read_text()) if base.with_suffix(".json").exists() else {}
    return base, frames, meta


def oracle_table(frame, high=False):
    """(info, table) from CameraEmulator for one (196, 256) uint16 frame."""
    with tempfile.NamedTemporaryFile(suffix=".npy", delete=False) as f:
        path = f.name
    np.save(path, frame)
    cam = ht301_hacklib.CameraEmulator(path)
    pathlib.Path(path).unlink()
    if high:  # temperature_range_high() without the zoom command (the mock would ignore it anyway)
        cam.correction_coefficient_m = 1.17
        cam.correction_coefficient_b = -40.9
    info, table = cam.info()
    return info, np.asarray(table)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("--frames", default="0", help="comma-separated frame indices")
    ap.add_argument("--golden", help="output prefix for golden files")
    ap.add_argument("--high", action="store_true", help="also write the ht301 high-range table")
    ap.add_argument("--at", action="append", default=[], help="x,y pixel to print (repeatable)")
    args = ap.parse_args()

    base, frames, _ = load_frames(args.dump)
    for i in (int(s) for s in args.frames.split(",")):
        frame = frames[i]
        info, table = oracle_table(frame)
        line = (f"{base.name} frame {i}: FPA {info['temp_fpa']:.3f} C, shutter {info['temp_shutter']:.2f} C, "
                f"emissivity {info['emissivity']:.3f}, distance {info['distance']}, "
                f"table dtype {table.dtype}, min {np.nanmin(table):.3f} C at raw {int(np.nanargmin(table))}")
        print(line)
        for xy in args.at:
            x, y = (int(v) for v in xy.split(","))
            raw = int(frame[y, x])
            print(f"  ({x},{y}) raw {raw} -> {table[raw]:.3f} C")
        if args.golden:
            prefix = pathlib.Path(f"{args.golden}_f{i:04d}")
            prefix.parent.mkdir(parents=True, exist_ok=True)
            frame[ROWS:, :].astype("<u2").tofile(prefix.with_suffix(".meta"))
            table.astype("<f4").tofile(prefix.with_suffix(".lut"))
            if args.high:
                _, high = oracle_table(frame, high=True)
                high.astype("<f4").tofile(prefix.with_suffix(".hi_ht301"))
            print(f"  wrote {prefix}.meta/.lut{'/.hi_ht301' if args.high else ''}")


if __name__ == "__main__":
    main()

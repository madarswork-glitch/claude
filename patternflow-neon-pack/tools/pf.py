#!/usr/bin/env python3
"""
Build, preview and package the Neon pack.

    python tools/pf.py sim <slug> [--landscape] [--frames N] [--abs i=v ...] [--sweep] [--time]
    python tools/pf.py previews            # GIF + contact sheet for every pattern
    python tools/pf.py build               # .pfm modules + both zips in dist/

Needs a Patternflow checkout (https://github.com/engmung/Patternflow) for the
module SDK headers and build_module.py: point PF_REPO at it. `build` also needs
an xtensa-esp32s3 GCC (the ESP32 Arduino core's, PlatformIO's, or any
directory named by PF_XTENSA_BIN) - the same compiler the community build
server uses.

Every pattern is written once against FRAME_W x FRAME_H. The build stamps each
source twice: PF_PORTRAIT 1 (64 x 128, the panel standing on its end) and
PF_PORTRAIT 0 (128 x 64, lying wide), so each orientation gets its own zip.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PACK = HERE.parent
PATTERNS = PACK / "patterns"
BUILD = PACK / ".build"
DIST = PACK / "dist"
PREVIEWS = PACK / "previews"

PANEL_W, PANEL_H = 128, 64
FIXED_DATE = (2026, 1, 1, 0, 0, 0)

# Running order on the device: alternate objects with fields so neighbours contrast.
ORDER = [
    "warp_tunnel",
    "tesseract",
    "kaleido_glass",
    "spiral_galaxy",
    "outrun",
    "lava_topo",
    "dna_helix",
    "op_art_moire",
    "phosphor_laser",
    "neon_circuit",
]


def pf_repo() -> Path:
    repo = Path(os.environ.get("PF_REPO", "")).expanduser()
    if not (repo / "firmware" / "patternflow" / "abi" / "pf_module.h").is_file():
        sys.exit("Set PF_REPO to a Patternflow checkout (git clone https://github.com/engmung/Patternflow)")
    return repo


def slugs() -> list[str]:
    found = sorted(p.name for p in PATTERNS.iterdir() if (p / "pattern.cpp").is_file())
    ordered = [s for s in ORDER if s in found]
    return ordered + [s for s in found if s not in ordered]


def stage(slug: str, portrait: bool, root: Path) -> Path:
    """Write <root>/<slug>/pattern.cpp with the orientation stamped in."""
    src = PATTERNS / slug
    out = root / slug
    out.mkdir(parents=True, exist_ok=True)
    body = (src / "pattern.cpp").read_text(encoding="utf-8")
    stamp = (
        f"// Built for a {'64 x 128 portrait' if portrait else '128 x 64 landscape'} frame.\n"
        f"#define PF_PORTRAIT {1 if portrait else 0}\n"
    )
    (out / "pattern.cpp").write_text(stamp + body, encoding="utf-8", newline="\n")
    shutil.copyfile(src / "module.json", out / "module.json")
    return out


# ── Native simulation ────────────────────────────────────────────────────────

def sim_binary(slug: str, portrait: bool) -> Path:
    repo = pf_repo()
    sketch = repo / "firmware" / "patternflow"
    tag = "p" if portrait else "l"
    staged = stage(slug, portrait, BUILD / f"sim_{tag}")
    exe = BUILD / f"sim_{tag}" / slug / "sim"
    cmd = [
        os.environ.get("CXX", "g++"), "-O2", "-std=gnu++17", "-w",
        "-DPF_MODULE_BUILD", f"-DPF_PANEL_W={PANEL_W}", f"-DPF_PANEL_H={PANEL_H}",
        "-DPF_ABI_MODULE_VERSION=2",
        f"-I{sketch / 'abi'}", f"-I{sketch / 'src'}",
        str(staged / "pattern.cpp"), str(HERE / "sim" / "harness.cpp"),
        "-o", str(exe), "-lm",
        *os.environ.get("SIM_FLAGS", "").split(),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"native compile failed for {slug}:\n{proc.stderr}")
    return exe


def run_sim(slug: str, portrait: bool, extra: list[str], out: Path | None) -> str:
    exe = sim_binary(slug, portrait)
    cmd = [str(exe), *extra]
    if out is not None:
        cmd += ["--out", str(out)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{slug}: simulator failed\n{proc.stderr}")
    return proc.stdout + proc.stderr


def load_frames(raw: Path, portrait: bool):
    import numpy as np
    data = np.fromfile(raw, dtype=np.uint8)
    n = data.size // (PANEL_W * PANEL_H * 3)
    panel = data[: n * PANEL_W * PANEL_H * 3].reshape(n, PANEL_H, PANEL_W, 3)
    if portrait:
        # The canvas maps frame (x, y) to panel (W-1-y, x); undo it to view upright.
        return panel[:, :, ::-1, :].transpose(0, 2, 1, 3)
    return panel


def led_look(frame, scale: int = 5):
    """Upscale with a dark gap between LEDs, and lift by 1/2.2 so a monitor shows
    what a linear-PWM panel (gamma 1.0, the firmware default) actually emits."""
    import numpy as np
    from PIL import Image
    f = (np.power(frame.astype(np.float32) / 255.0, 1 / 2.2) * 255.0).astype(np.uint8)
    h, w, _ = f.shape
    big = np.zeros((h * scale, w * scale, 3), dtype=np.uint8)
    for dy in range(scale - 1):
        for dx in range(scale - 1):
            big[dy::scale, dx::scale] = f
    return Image.fromarray(big)


def make_preview(slug: str, portrait: bool, frames, gif: Path, sheet: Path | None,
                 fps: float, every: int = 1, scale: int = 4) -> None:
    from PIL import Image
    imgs = [led_look(frames[i], scale) for i in range(0, len(frames), every)]
    imgs[0].save(gif, save_all=True, append_images=imgs[1:],
                 duration=int(1000 * every / fps), loop=0, optimize=False)
    if sheet is not None:
        picks = [int(i * (len(frames) - 1) / 7) for i in range(8)]
        tiles = [led_look(frames[i], 4) for i in picks]
        tw, th = tiles[0].size
        cols = 8 if portrait else 4
        rows = (len(tiles) + cols - 1) // cols
        canvas = Image.new("RGB", (cols * (tw + 6), rows * (th + 6)), (40, 40, 40))
        for i, t in enumerate(tiles):
            canvas.paste(t, ((i % cols) * (tw + 6), (i // cols) * (th + 6)))
        canvas.save(sheet)


# ── Cross build + pack ───────────────────────────────────────────────────────

def build_pack(portrait: bool) -> Path:
    repo = pf_repo()
    tag = "portrait" if portrait else "landscape"
    src_root = BUILD / f"pfm_{tag}" / "src"
    out_root = BUILD / f"pfm_{tag}" / "out"
    shutil.rmtree(BUILD / f"pfm_{tag}", ignore_errors=True)
    names = slugs()
    dirs = [stage(s, portrait, src_root) for s in names]
    cmd = [sys.executable, str(repo / "firmware" / "toolchain" / "build_module.py"),
           "--out", str(out_root), *map(str, dirs)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout, end="")
    if proc.returncode != 0:
        print(proc.stderr, end="")
        sys.exit(f"module build failed ({tag})")

    DIST.mkdir(exist_ok=True)
    zpath = DIST / f"neon-pack-{tag}.zip"
    metas = []
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        def write(name: str, data: bytes) -> None:
            info = zipfile.ZipInfo(name, date_time=FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, data)

        for s in names:
            meta = json.loads((out_root / f"{s}.json").read_text(encoding="utf-8"))
            if not meta.get("author") or meta["author"] == "unknown":
                sys.exit(f"{s}: module.json has no author")
            for k in ("source", "opt"):
                meta.pop(k, None)
            metas.append(meta)
            write(f"{s}.pfm", (out_root / f"{s}.pfm").read_bytes())
            write(f"{s}.json", (json.dumps(meta, indent=2, ensure_ascii=False) + "\n").encode())
        write("catalog.txt", (
            "# Patternflow running order - one module slug per line.\n"
            "# The device reads this on install; delete it to sort by name instead.\n"
            + "\n".join(names) + "\n").encode())
        write("README.txt", "\n".join([
            f"Patternflow - Neon pack ({tag}, {'64 x 128' if portrait else '128 x 64'})",
            "",
            f"{len(names)} patterns, in running order:",
            *[f"  {m['name']:<16} knobs: {' / '.join(m['knobs'])}" for m in metas],
            "",
            "To install: open your board's Patterns page (http://patternflow.local/patterns)",
            "and drop this .zip on the upload area. Don't unzip it.",
            "Needs firmware v3.10.5 or newer.",
            "",
            f"Author:  {', '.join(sorted({m['author'] for m in metas}))}",
            f"Licence: {', '.join(sorted({m['license'] for m in metas}))}",
            "",
        ]).encode())
    total = sum(m["size"] for m in metas)
    print(f"{zpath.name}: {len(names)} modules, {total / 1024:.0f} KB of .pfm, zip {zpath.stat().st_size / 1024:.0f} KB")
    return zpath


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sim")
    s.add_argument("slug")
    s.add_argument("--landscape", action="store_true")
    s.add_argument("--frames", type=int, default=150)
    s.add_argument("--fps", type=float, default=30)
    s.add_argument("--abs", action="append", default=[])
    s.add_argument("--turn", action="append", default=[])
    s.add_argument("--sweep", action="store_true")
    s.add_argument("--time", action="store_true")
    s.add_argument("--name", default=None, help="Output basename (default: slug)")
    s.add_argument("--every", type=int, default=1, help="Keep every Nth frame in the GIF")
    sub.add_parser("previews")
    sub.add_parser("build")
    args = ap.parse_args()

    if args.cmd == "sim":
        portrait = not args.landscape
        extra = ["--frames", str(args.frames), "--fps", str(args.fps)]
        for a in args.abs:
            extra += ["--abs", a]
        for t in args.turn:
            extra += ["--turn", t]
        if args.sweep:
            extra.append("--sweep")
        if args.time:
            extra.append("--time")
        raw = BUILD / f"{args.slug}.rgb"
        print(run_sim(args.slug, portrait, extra, raw), end="")
        frames = load_frames(raw, portrait)
        name = args.name or args.slug
        out = BUILD / "look"
        out.mkdir(parents=True, exist_ok=True)
        make_preview(args.slug, portrait, frames, out / f"{name}.gif", out / f"{name}.png",
                     args.fps, args.every)
        print(f"-> {out / (name + '.png')}")
    elif args.cmd == "previews":
        from PIL import Image
        PREVIEWS.mkdir(exist_ok=True)
        n, every, scale = 150, 2, 3
        for portrait in (True, False):
            tag = "portrait" if portrait else "landscape"
            clips = []
            for slug in slugs():
                raw = BUILD / f"{slug}.rgb"
                run_sim(slug, portrait, ["--frames", str(n), "--fps", "30"], raw)
                frames = load_frames(raw, portrait)
                make_preview(slug, portrait, frames, PREVIEWS / f"{slug}-{tag}.gif", None, 30, every, scale)
                clips.append(frames)
                print(f"{slug} {tag}")
            # Overview: all ten side by side, one GIF.
            cols = 5
            h, w, _ = clips[0][0].shape
            pad = 6
            sheet = []
            for f in range(0, n, every):
                tiles = [led_look(c[f], 2) for c in clips]
                tw, th = tiles[0].size
                img = Image.new("RGB", (cols * (tw + pad) + pad, 2 * (th + pad) + pad), (24, 24, 24))
                for i, t in enumerate(tiles):
                    img.paste(t, (pad + (i % cols) * (tw + pad), pad + (i // cols) * (th + pad)))
                sheet.append(img)
            sheet[0].save(PREVIEWS / f"overview-{tag}.gif", save_all=True, append_images=sheet[1:],
                          duration=int(1000 * every / 30), loop=0)
    elif args.cmd == "build":
        for portrait in (True, False):
            build_pack(portrait)


if __name__ == "__main__":
    main()

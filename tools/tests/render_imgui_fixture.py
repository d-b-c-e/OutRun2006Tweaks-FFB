"""Rasterize actual ImGui draw lists, offline; no game, window or GPU required."""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image


def render(path: Path, atlas: np.ndarray):
    frame = json.loads(path.read_text())
    width, height = map(int, frame["size"])
    canvas = np.empty((height, width, 4), np.float32)
    canvas[:] = [17, 20, 24, 255]
    ah, aw, _ = atlas.shape
    triangles = 0
    for draw in frame["lists"]:
        vertices = np.asarray(draw["vertices"], dtype=np.float64)
        if not len(vertices):
            continue
        packed = vertices[:, 4].astype(np.uint32)
        colors = np.stack([(packed >> s) & 255 for s in (0, 8, 16, 24)], axis=1)
        indices = np.asarray(draw["indices"], dtype=np.int32)
        for offset, base, count, cx0, cy0, cx1, cy1 in draw["commands"]:
            for ids in indices[int(offset):int(offset + count)].reshape(-1, 3) + int(base):
                v, color = vertices[ids], colors[ids]
                x0 = max(0, math.floor(max(cx0, v[:, 0].min())))
                y0 = max(0, math.floor(max(cy0, v[:, 1].min())))
                x1 = min(width, math.ceil(min(cx1, v[:, 0].max())))
                y1 = min(height, math.ceil(min(cy1, v[:, 1].max())))
                if x0 >= x1 or y0 >= y1:
                    continue
                a, b, c = v[:, :2]
                denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(denominator) < 1e-8:
                    continue
                yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
                xx += .5
                yy += .5
                wa = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denominator
                wb = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denominator
                wc = 1 - wa - wb
                mask = (wa >= -1e-5) & (wb >= -1e-5) & (wc >= -1e-5)
                weights = np.stack((wa, wb, wc), axis=2)
                uv = weights @ v[:, 2:4]
                # The game backend uses linear texture sampling. Reproduce it
                # so scaled glyphs reflect the production font rather than a
                # misleading nearest-neighbour preview.
                txf = np.clip(uv[:, :, 0] * aw - .5, 0, aw - 1)
                tyf = np.clip(uv[:, :, 1] * ah - .5, 0, ah - 1)
                tx, ty = txf.astype(int), tyf.astype(int)
                tx1, ty1 = np.minimum(tx + 1, aw - 1), np.minimum(ty + 1, ah - 1)
                fx, fy = (txf - tx)[:, :, None], (tyf - ty)[:, :, None]
                texture = ((atlas[ty, tx] * (1 - fx) + atlas[ty, tx1] * fx) * (1 - fy)
                           + (atlas[ty1, tx] * (1 - fx) + atlas[ty1, tx1] * fx) * fy)
                rgba = (weights @ color) * texture / 255
                alpha = rgba[:, :, 3:4] / 255 * mask[:, :, None]
                destination = canvas[y0:y1, x0:x1]
                destination[:, :, :3] = rgba[:, :, :3] * alpha + destination[:, :, :3] * (1 - alpha)
                triangles += 1
    output = path.with_suffix(".png")
    Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8)).save(output)
    return {"image": str(output), "viewport": [width, height], "fontScale": frame["scale"], "windows": frame["windows"], "triangles": triangles,
            "source": "Production ImGui draw data with mocked devices; not an in-game screenshot"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    metadata = json.loads((args.directory / "font-atlas.json").read_text())
    atlas = np.fromfile(args.directory / "font-atlas.rgba", dtype=np.uint8).reshape(metadata["height"], metadata["width"], 4).astype(np.float32)
    paths = [p for p in args.directory.glob("*.json") if p.name not in ("font-atlas.json", "render-manifest.json")]
    evidence = [render(path, atlas) for path in paths]
    (args.directory / "render-manifest.json").write_text(json.dumps(evidence, indent=2))
    print(f"Rendered {len(evidence)} production ImGui fixtures in {args.directory}")


if __name__ == "__main__":
    main()

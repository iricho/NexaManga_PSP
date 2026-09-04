#!/usr/bin/env python3
"""Generate a deterministic MangaPSP hardware-acceptance library.

Requires Pillow. The default output is intentionally substantial; use --quick
for a fast generator smoke test, not for hardware performance measurements.
"""

from __future__ import annotations

import argparse
import io
import shutil
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw


RESOLUTIONS = [
    (1200, 1800),
    (1400, 2100),
    (1600, 2400),
    (1800, 2700),
    (2000, 3000),
]


def page(width: int, height: int, mode: str, number: int) -> Image.Image:
    image = Image.new(mode, (width, height), 236 if mode == "L" else (236, 232, 222))
    draw = ImageDraw.Draw(image)
    step = max(12, height // 40)
    for y in range(0, height, step):
        shade = (y // step * 37 + number * 19) % 180
        color = shade if mode == "L" else (shade, (shade * 3) % 220, 220 - shade)
        draw.rectangle((0, y, width, min(height, y + max(2, step // 3))), fill=color)
    margin = max(8, width // 24)
    draw.rectangle((margin, margin, width - margin, height - margin), outline=0, width=max(2, width // 300))
    draw.text((margin * 2, margin * 2), f"MangaPSP {width}x{height} {mode} #{number}", fill=0)
    return image


def jpeg_bytes(image: Image.Image, *, progressive: bool = False) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", quality=86, optimize=False, progressive=progressive)
    return buffer.getvalue()


def png_bytes(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, "PNG", compress_level=6)
    return buffer.getvalue()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_resolution_series(root: Path, quick: bool) -> list[bytes]:
    samples: list[bytes] = []
    dimensions = [(max(120, w // 10), max(160, h // 10)) for w, h in RESOLUTIONS] if quick else RESOLUTIONS
    for index, (width, height) in enumerate(dimensions, 1):
        for mode, label in (("L", "Grayscale"), ("RGB", "RGB")):
            image = page(width, height, mode, index)
            baseline = jpeg_bytes(image)
            samples.append(baseline)
            chapter = root / f"{label}_{width}x{height}"
            write(chapter / "001_baseline.jpg", baseline)
            write(chapter / "002_progressive.jpg", jpeg_bytes(image, progressive=True))
            if index in (1, len(dimensions)):
                write(chapter / "003_fallback.png", png_bytes(image))
    return samples


def make_cbz_cases(root: Path, samples: list[bytes], large_pages: int) -> None:
    mixed = root / "CBZ_Cases"
    mixed.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(mixed / "Valid_Nested.cbz", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("pages/10.jpg", samples[1 % len(samples)])
        archive.writestr("pages/2.jpg", samples[0])
        archive.writestr("pages/readme.txt", "ignored")
        archive.writestr("__MACOSX/._2.jpg", b"ignored")
    with zipfile.ZipFile(mixed / "Large.cbz", "w", zipfile.ZIP_DEFLATED) as archive:
        for index in range(large_pages):
            archive.writestr(f"deep/pages/{index + 1:04d}.jpg", samples[index % len(samples)])
    write(mixed / "Corrupt.cbz", b"This is intentionally not a ZIP archive.")
    valid = (mixed / "Valid_Nested.cbz").read_bytes()
    write(mixed / "Truncated.cbz", valid[: max(32, len(valid) // 3)])


def make_failure_cases(root: Path, sample: bytes) -> None:
    failures = root / "Recoverable_Page_Errors"
    write(failures / "001_valid.jpg", sample)
    write(failures / "002_corrupt.jpg", b"not an image")
    write(failures / "003_truncated.jpg", sample[:128])
    write(failures / "004_empty.png", b"")
    write(failures / "005_valid.jpg", sample)


def make_metadata_stress(root: Path, sample: bytes, chapters: int) -> None:
    series = root / "Metadata_Stress_Long_Series_Name_0123456789"
    for index in range(chapters):
        chapter = series / f"Chapter_{index + 1:03d}_Long_Name_0123456789"
        write(chapter / "page_0001.jpg", sample)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True,
                        help="Destination directory; MANGA/ is created inside it")
    parser.add_argument("--quick", action="store_true",
                        help="Scale images down and reduce counts for a generator smoke test")
    parser.add_argument("--large-cbz-pages", type=int, default=30)
    parser.add_argument("--stress-chapters", type=int, default=80)
    parser.add_argument("--force", action="store_true",
                        help="Replace an existing generated MANGA/Acceptance_Test_Library tree")
    args = parser.parse_args()
    base = args.output.resolve() / "MANGA" / "Acceptance_Test_Library"
    if base.exists():
        if not args.force:
            parser.error(f"{base} already exists; pass --force to replace it")
        shutil.rmtree(base)
    base.mkdir(parents=True)

    samples = make_resolution_series(base / "Resolution_Matrix", args.quick)
    make_cbz_cases(base, samples, min(args.large_cbz_pages, 5) if args.quick else args.large_cbz_pages)
    make_failure_cases(base, samples[0])
    make_metadata_stress(base, samples[0], min(args.stress_chapters, 5) if args.quick else args.stress_chapters)
    print(base)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

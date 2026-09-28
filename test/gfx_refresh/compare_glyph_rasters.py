"""Compare complete BW/gray page bytes from old/new production renderer builds."""

import csv
import hashlib
import io
import pathlib
import subprocess
import sys
import tempfile

reference, current = sys.argv[1:3]
samples = int(sys.argv[3]) if len(sys.argv) > 3 else 1
with tempfile.TemporaryDirectory(prefix="crosspoint-glyph-") as temporary:
    results = []
    for name, executable in (("r11", reference), ("optimized", current)):
        output = pathlib.Path(temporary) / (name + ".bin")
        run = subprocess.run([executable, str(output), str(samples)], check=True, capture_output=True, text=True)
        results.append((list(csv.DictReader(io.StringIO(run.stdout))), output.read_bytes()))
    old, new = results
    if len(old[0]) != 192 or len(new[0]) != 192:
        raise SystemExit("Expected 192 raster scenarios in both renderer builds")
    keys = ("scene", "orientation", "font_cache", "gray_mode", "clip", "strip_rows", "samples")
    for old_row, new_row in zip(old[0], new[0]):
        if any(old_row[key] != new_row[key] for key in keys):
            raise SystemExit("Raster scenario ordering differs")
    if len(old[1]) != 192 * 3 * 48000 or len(new[1]) != len(old[1]):
        raise SystemExit("Incomplete full-frame raster output")
    if old[1] != new[1]:
        first = next(i for i, (a, b) in enumerate(zip(old[1], new[1])) if a != b)
        case = old[0][first // (3 * 48000)]
        raise SystemExit(f"Raster mismatch at byte {first}: {case}")
    print(f"# 192 scenarios; {len(old[1])} complete frame/plane bytes identical; sha256={hashlib.sha256(old[1]).hexdigest()}")
    print("# Host CPU only; excludes prewarm, SD I/O, SPI, panel BUSY and optical behavior.")
    writer = csv.writer(sys.stdout)
    writer.writerow([*keys, "r11_bw_us", "optimized_bw_us", "bw_ratio", "r11_gray_us", "optimized_gray_us", "gray_ratio"])
    for old_row, new_row in zip(old[0], new[0]):
        bw_old, bw_new = float(old_row["bw_median_us"]), float(new_row["bw_median_us"])
        gray_old, gray_new = float(old_row["gray_median_us"]), float(new_row["gray_median_us"])
        writer.writerow([*[old_row[key] for key in keys], bw_old, bw_new, round(bw_old / bw_new, 3),
                         gray_old, gray_new, round(gray_old / gray_new, 3)])

#!/usr/bin/env python3
"""Reproduce Libron assets and compare every glyph with its source font.

Use --write to regenerate headers/results. An optional --baseline-dir holds the
pre-fallback headers, for an additional comparison against those exact assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
import zlib

HERE = Path(__file__).resolve().parent
BUILTINS = HERE.parent.parent
SCRIPTS = BUILTINS.parent / "scripts"
sys.path.insert(0, str(SCRIPTS))
import verify_compression as compression


def digest(data):
    return hashlib.sha256(data).hexdigest()


def array(text, name):
    match = re.search(r"static const [\w]+ " + re.escape(name) +
                      r"(?:\[[^\]]*\])?\s*=\s*\{(.*?)\};", text, re.S)
    assert match, name
    return match.group(1)


def font_metrics(text, name):
    fields = array(text, name).split(',')
    return tuple(int(field.strip()) for field in fields[4:7])


def parse(text, name):
    bitmaps = compression.parse_hex_array(array(text, name + "Bitmaps"))
    glyphs = compression.parse_glyphs(array(text, name + "Glyphs"))
    intervals = re.findall(r"\{\s*(0x[\dA-F]+),\s*(0x[\dA-F]+),\s*(0x[\dA-F]+)\s*\}",
                           array(text, name + "Intervals"))
    cps = []
    for first, last, offset in intervals:
        assert int(offset, 16) == len(cps)
        cps.extend(range(int(first, 16), int(last, 16) + 1))
    assert len(cps) == len(glyphs)
    groups = []
    if name + "Groups[]" in text:
        groups = compression.parse_groups(array(text, name + "Groups"))
    pixels = {}
    if groups:
        for group in groups:
            start = group['compressedOffset']
            data = zlib.decompress(bitmaps[start:start + group['compressedSize']], -15)
            assert len(data) == group['uncompressedSize']
            cursor = 0
            for index in range(group['firstGlyphIndex'], group['firstGlyphIndex'] + group['glyphCount']):
                glyph = glyphs[index]
                length = ((glyph['width'] + 3) // 4) * glyph['height']
                pixels[cps[index]] = compression.compact_aligned_to_packed(
                    data[cursor:cursor + length], glyph['width'], glyph['height'])
                cursor += length
            assert cursor == len(data)
    else:
        for cp, glyph in zip(cps, glyphs):
            start = glyph['dataOffset']
            pixels[cp] = bitmaps[start:start + glyph['dataLength']]
    parsed = {}
    for cp, glyph in zip(cps, glyphs):
        metrics = tuple(glyph[key] for key in ('width', 'height', 'advanceX', 'left', 'top', 'dataLength'))
        parsed[cp] = (metrics, pixels[cp])
    assert len(parsed) == len(cps) == len(pixels)
    return parsed, groups, len(bitmaps)


def convert(name, size, paths, compressed):
    args = ['fontconvert.py', name, str(size), *paths, '--2bit']
    if compressed:
        args += ['--compress']
    args += ['--pnum']
    if compressed:
        args += ['--zopfli']
    result = subprocess.run([sys.executable, *args], cwd=SCRIPTS, capture_output=True, check=True)
    assert b'WARNING' not in result.stderr, result.stderr.decode()
    return result.stdout, result.stderr.decode(), 'python ' + shlex.join(args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--baseline-dir', type=Path)
    args = parser.parse_args()
    started = time.monotonic()
    records = []
    for size in (12, 14, 16, 18):
        for style in ('Regular', 'Italic', 'Bold', 'BoldItalic'):
            name = f'libron_{size}_{style.lower()}'
            header = BUILTINS / (name + '.h')
            paths = [f'../builtinFonts/source/Libron/Libron-{style}.ttf',
                     f'../builtinFonts/source/NotoSans/NotoSans-{style}.ttf']
            data, stderr, command = convert(name, size, paths, True)
            repeated, _, _ = convert(name, size, paths, True)
            assert data == repeated, f'{name}: regeneration differs'
            if args.write:
                header.write_bytes(data)
            else:
                assert header.read_bytes() == data, f'{name}: checked-in header differs'
            _, success, message = compression.verify_font_file(header)
            assert success, message
            combined, groups, bitmap_bytes = parse(data.decode(), name)
            plain, _, _ = convert(name, size, paths, False)
            uncompressed, _, _ = parse(plain.decode(), name)
            assert combined == uncompressed, f'{name}: compressed/uncompressed pixels or metrics differ'
            native_data, _, _ = convert(name, size, paths[:1], False)
            native, _, _ = parse(native_data.decode(), name)
            assert font_metrics(data.decode(), name) == font_metrics(native_data.decode(), name)
            fallback_data, _, _ = convert(name, size, paths[1:], False)
            fallback, _, _ = parse(fallback_data.decode(), name)
            assert combined.keys() == native.keys() | fallback.keys()
            for cp, glyph in native.items():
                assert combined[cp] == glyph, f'{name}: native U+{cp:04X} differs'
            added = combined.keys() - native.keys()
            for cp in added:
                assert combined[cp] == fallback[cp], f'{name}: fallback U+{cp:04X} differs'
            # Cyrillic and the review's punctuation/spacing/mark/replacement cases.
            for cp in (0x0410, 0x0430, 0x0315, 0x034F, 0x2007, 0x2011, 0x202F, 0x2060, 0xFFFD):
                assert cp in added, f'{name}: U+{cp:04X} missing from fallback'
            classes = re.search(r'kerning: (\d+) left classes, (\d+) right classes', stderr)
            left, right = map(int, classes.groups())
            assert max(left, right) <= 255
            for suffix in ('KernLeftClassIds', 'KernRightClassIds', 'KernSparseCols'):
                assert max(compression.parse_uint8_array(array(data.decode(), name + suffix))) <= 255
            assert max(compression.parse_uint8_array(array(data.decode(), name + 'KernRowOffsets'))) <= 65535
            assert max(g['uncompressedSize'] for g in groups) <= 65536
            record = dict(header=header.name, command=command, sha256=digest(data),
                          header_bytes=len(data), stderr=stderr, bitmap_bytes=bitmap_bytes,
                          glyph_count=len(combined), native_glyph_count=len(native),
                          fallback_glyph_count=len(added), group_count=len(groups),
                          max_group_uncompressed_bytes=max(g['uncompressedSize'] for g in groups),
                          kerning_left_classes=left, kerning_right_classes=right,
                          byte_identical_regeneration=True,
                          uncompressed_pixel_and_metric_parity_glyphs=len(combined),
                          native_source_pixel_and_metric_parity_glyphs=len(native),
                          fallback_source_pixel_and_metric_parity_glyphs=len(added))
            if args.baseline_dir:
                baseline_path = args.baseline_dir / header.name
                baseline, _, _ = parse(baseline_path.read_text(), name)
                assert font_metrics(data.decode(), name) == font_metrics(baseline_path.read_text(), name)
                assert baseline == native, f'{name}: original native baseline differs from source'
                for cp, glyph in baseline.items():
                    assert combined[cp] == glyph, f'{name}: original U+{cp:04X} differs'
                record['native_baseline_sha256'] = digest(baseline_path.read_bytes())
                record['native_baseline_pixel_and_metric_parity_glyphs'] = len(baseline)
                record['native_baseline_font_line_metrics_unchanged'] = True
            records.append(record)
            print(f'{name}: {len(native)} native + {len(added)} fallback; classes {left}/{right}', flush=True)
    results = dict(elapsed_seconds=round(time.monotonic() - started, 2), headers=records,
                   verification=dict(compressed_headers_passed=len(records),
                                     byte_identical_regenerations=len(records),
                                     uncompressed_pixel_and_metric_parity_headers=len(records),
                                     converter_sha256=digest((SCRIPTS / 'fontconvert.py').read_bytes())))
    if args.write:
        (HERE / 'generation-results.json').write_text(json.dumps(results, indent=2) + '\n')
        (HERE / 'GENERATED-SHA256SUMS').write_text(''.join(
            f"{r['sha256']}  ../../{r['header']}\n" for r in records))
    print(json.dumps(results['verification'], indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Compare actual pre-3521 and working-tree SdCardFont code with deterministic SD I/O."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
BASELINE = "e5dcc64fd4f9cfefc33f1a0917ea1fb2a5ca183b"
FONT_FILES = ("SdCardFont.h", "SdCardFont.cpp")


def run(command, **kwargs):
    return subprocess.check_output(command, cwd=REPO, **kwargs)


def benchmark(source, output, compiler, repeats):
    legacy = "bool accumulate" not in (source / "SdCardFont.h").read_text()
    command = compiler + ["-std=c++20", "-O2", "-fno-exceptions", "-DNDEBUG"]
    if legacy:
        command.append("-DSD_FONT_LEGACY_PREWARM=1")
    includes = (
        source,
        REPO / "test/sd_card_font/stubs",
        REPO / "lib/EpdFont",
        REPO / "lib/Utf8",
        REPO / "lib/Memory",
    )
    for include in includes:
        command += ["-I", str(include)]
    command += [
        str(REPO / "test/sd_card_font/SdCardFontBenchmark.cpp"),
        str(REPO / "test/sd_card_font/HostAllocations.cpp"),
        str(source / "SdCardFont.cpp"),
        str(REPO / "lib/Utf8/Utf8.cpp"),
        "-o", str(output),
    ]
    subprocess.run(command, cwd=REPO, check=True)
    samples = [
        [json.loads(line) for line in run([str(output)], text=True).splitlines()]
        for _ in range(repeats)
    ]
    for sample in samples[1:]:
        if len(sample) != len(samples[0]):
            raise RuntimeError("Benchmark row count varied")
        for first, row in zip(samples[0], sample):
            for key, value in first.items():
                if key != "host_prewarm_ns" and row[key] != value:
                    raise RuntimeError(f"Nondeterministic counter: {key}")
    rows = []
    for index, first in enumerate(samples[0]):
        row = {key: value for key, value in first.items() if key != "host_prewarm_ns"}
        row["host_prewarm_median_ns"] = statistics.median(sample[index]["host_prewarm_ns"] for sample in samples)
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", default=BASELINE, help="Git revision containing the original production source")
    parser.add_argument("--repeats", type=int, default=7, help="Host process repetitions; I/O counts must match each time")
    parser.add_argument("--output", type=Path, help="Write JSON here (stdout when omitted)")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    baseline_commit = run(["git", "rev-parse", "--verify", args.baseline_ref + "^{commit}"], text=True).strip()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    hashes = {}
    with tempfile.TemporaryDirectory(prefix="crosspoint-font-benchmark-") as temporary:
        scratch = Path(temporary)
        baseline_source = scratch / "baseline"
        baseline_source.mkdir()
        for name in FONT_FILES:
            content = run(["git", "show", f"{baseline_commit}:lib/EpdFont/{name}"])
            (baseline_source / name).write_bytes(content)
            hashes[name] = {
                "baseline_sha256": hashlib.sha256(content).hexdigest(),
                "current_sha256": hashlib.sha256((REPO / "lib/EpdFont" / name).read_bytes()).hexdigest(),
            }
        baseline_rows = benchmark(baseline_source, scratch / "baseline-benchmark", compiler, args.repeats)
        current_rows = benchmark(REPO / "lib/EpdFont", scratch / "current-benchmark", compiler, args.repeats)
    report = {
        "schema_version": 1,
        "baseline_commit": baseline_commit,
        "working_tree_head": run(["git", "rev-parse", "HEAD"], text=True).strip(),
        "production_source_hashes": hashes,
        "compiler": run(compiler + ["--version"], text=True).splitlines()[0],
        "host_repeats": args.repeats,
        "measurement_scope": "Production SdCardFont.cpp; HAL uses an in-memory CPFONT v4 fixture. Counts exclude font loading.",
        "fixture": "513 Hangul/replacement glyphs, 32x32 monochrome, 128 bitmap bytes each, reverse bitmap order; 1 or 4 distinct styles; no kern/ligatures.",
        "timing_limit": "Host CPU time with memory-backed SD; excludes hardware SD latency, rendering, panel waveform and BUSY time. Not device page-turn milliseconds.",
        "memory_limit": "Heap availability is a configured test input (200 KiB). Allocation counters cover nothrow new[] calls and bytes requested, not peak live heap, allocator overhead or ESP32 fragmentation.",
        "compatibility": "Baseline invokes the original 4-argument prewarm API; current complete-page prewarm passes accumulate=false, matching the reader call sites.",
        "baseline": baseline_rows,
        "current": current_rows,
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()

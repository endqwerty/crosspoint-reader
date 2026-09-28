#!/usr/bin/env python3
"""Run recorded-bus workloads against unmodified production X4 Pro drivers."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LIB = REPO / "freeink-sdk/libs/display/FreeInkDisplay"
DRIVERS = ("Ssd1677", "Uc8179", "Uc8279X4")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, help="write a JSON report with source provenance")
    parser.add_argument("--sanitize", action="store_true", help="enable address/undefined sanitizers")
    args = parser.parse_args()
    sources = ["src/FreeInkDisplay.cpp", "src/driver/PanelDriver.h",
               "include/FreeInkDisplay.h", "include/GrayscaleCapabilities.h",
               "src/lut/Ssd1677Luts.h", "src/lut/Uc8279X3Luts.h", "src/lut/UltraChipDirectGrayLuts.h"]
    sources += [f"src/driver/{driver}Driver.{ext}" for driver in DRIVERS for ext in ("cpp", "h")]
    report = {
        "schema": 1,
        "description": "Actual production driver command/payload counts; no optical or device timing simulation.",
        "source_sha256": {str((LIB / p).relative_to(REPO)): hashlib.sha256((LIB / p).read_bytes()).hexdigest()
                          for p in sources},
        "workloads": [],
    }
    test_inputs = [HERE / "RefreshSequences.cpp", HERE / "run.py",
                   REPO / "src/activities/reader/ReaderGrayscalePlan.h",
                   REPO / "test/reader_grayscale_plan/stubs/HalDisplay.h"]
    test_inputs += list((LIB / "test/host/pro_stubs").glob("*.h"))
    report["source_sha256"].update({str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in test_inputs})
    with tempfile.TemporaryDirectory(prefix="crosspoint-refresh-sequences-") as directory:
        root = Path(directory)
        for path in sources:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(LIB / path, target)
        stubs = LIB / "test/host/pro_stubs"
        for name in ("Arduino.h", "BoardConfig.h", "SPI.h", "esp_heap_caps.h"):
            shutil.copy2(stubs / name, root / name)
        (root / "src/bus").mkdir(parents=True)
        shutil.copy2(stubs / "EpdBus.h", root / "src/bus/EpdBus.h")
        for single in (True, False):
            exe = root / ("single" if single else "dual")
            command = shlex.split(os.environ.get("CXX", "c++"))
            command += ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-unused-function", "-DBOARD_HAS_PSRAM=1", "-DARDUINO=1",
                        "-I" + str(root), "-I" + str(root / "include"),
                        "-I" + str(REPO / "src/activities/reader"),
                        "-I" + str(REPO / "test/reader_grayscale_plan/stubs")]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if single:
                command += ["-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1"]
            command += [str(HERE / "RefreshSequences.cpp"), str(root / "src/FreeInkDisplay.cpp")]
            command += [str(root / f"src/driver/{driver}Driver.cpp") for driver in DRIVERS]
            subprocess.run(command + ["-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], stdout=subprocess.PIPE, text=True, check=True)
            report["workloads"].extend(json.loads(line) for line in result.stdout.splitlines())
    report["status"] = "passed"
    output = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()

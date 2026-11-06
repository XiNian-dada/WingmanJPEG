#!/usr/bin/env python3
"""Build unmodified/patched 3.2.1 and run an offline, paired benchmark.

Uses the exact replacement strings in the production build.rs. Requires a new
output directory, Python 3, CMake, GCC, GNU nm/objcopy, and upstream SIMD tools.
All build products and results stay below the requested output directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SHA = "6f5819c51649bce2571a23b1b0cd41eae2f57fc3f8908c7ec9e7047efb65ca13"


def run(args, **kwargs):
    return subprocess.run([str(v) for v in args], check=True, **kwargs)


def build(output, simd):
    archive = ROOT / "vendor/libjpeg-turbo-3.2.1.tar.gz"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        raise ValueError("pinned archive hash mismatch")
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            target = (output / member.name).resolve()
            if output not in target.parents or member.issym() or member.islnk():
                raise ValueError("unexpected archive path/link")
        source.extractall(output)
    stock = output / "libjpeg-turbo-3.2.1"
    patched = output / "patched-source"
    shutil.copytree(stock, patched)
    path = patched / "src/jcdctmgr.c"
    original = path.read_text()
    text = original
    rust = (ROOT / "build.rs").read_text()
    replacements = re.findall(
        r'replace\(&mut text,\s*("(?:[^"\\]|\\.)*"),\s*(r#".*?"#|"(?:[^"\\]|\\.)*")\);',
        rust, re.S,
    )
    if len(replacements) != 5:
        raise ValueError("production patch changed; review benchmark extraction")
    for old, new in replacements:
        old = json.loads(old)
        new = new[3:-2] if new.startswith('r#"') else json.loads(new)
        if text.count(old) != 1:
            raise ValueError("unexpected pinned patch location")
        text = text.replace(old, new, 1)
    path.write_text(text)
    shutil.copy2(ROOT / "flat_core.h", patched / "src/flat_core.h")
    records = {}
    for name, tree in [("stock", stock), ("flat", patched)]:
        build_dir = output / (name + "-build")
        run(["cmake", "-S", tree, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release",
             "-DENABLE_SHARED=OFF", "-DENABLE_STATIC=ON", "-DWITH_TURBOJPEG=OFF",
             "-DWITH_TOOLS=OFF", "-DWITH_TESTS=OFF", "-DWITH_SIMD=" + simd, "-DREQUIRE_SIMD=" + simd])
        run(["cmake", "--build", build_dir, "--target", "jpeg-static", "-j2"])
        library = build_dir / "libjpeg.a"
        symbols = subprocess.check_output(["nm", "-g", "--defined-only", str(library)], text=True)
        names = set()
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) == 3 and re.fullmatch(r"[A-Za-z_][A-Za-z_0-9.]*", parts[-1]):
                names.add(parts[-1])
        names.update(["wk_jpeg_flat_marker", "wk_jpeg_process", "wk_jpeg_free"])
        mapping = output / (name + ".symbols")
        mapping.write_text("".join(f"{symbol} {name}_{symbol}\n" for symbol in sorted(names)))
        prefixed = output / (name + "-prefixed.a")
        run(["objcopy", "--redefine-syms=" + str(mapping), library, prefixed])
        header = output / (name + "-rename.h")
        header.write_text("".join(f"#define {symbol} {name}_{symbol}\n" for symbol in sorted(names)))
        wrapper = output / (name + "-native.o")
        run(["cc", "-O3", "-Wall", "-Wextra", "-Werror", "-include", header,
             "-I" + str(build_dir), "-I" + str(tree / "src"), "-I" + str(ROOT),
             "-c", ROOT / "native.c", "-o", wrapper])
        records[name] = {"source_jcdctmgr_sha256": hashlib.sha256((tree / "src/jcdctmgr.c").read_bytes()).hexdigest(),
                         "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
                         "wrapper_sha256": hashlib.sha256(wrapper.read_bytes()).hexdigest()}
    binary = output / "compare"
    run(["cc", "-O3", "-Wall", "-Wextra", "-Werror", "-I" + str(output / "stock-build"),
         "-I" + str(stock / "src"), ROOT / "bench/compare.c",
         output / "stock-native.o", output / "flat-native.o",
         output / "stock-prefixed.a", output / "flat-prefixed.a", "-lm", "-o", binary])
    records["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    records["production_build_rs_sha256"] = hashlib.sha256(rust.encode()).hexdigest()
    records["production_flat_core_sha256"] = hashlib.sha256((ROOT / "flat_core.h").read_bytes()).hexdigest()
    return binary, stock, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new task-owned output directory")
    parser.add_argument("--rounds", type=int, default=31)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--quick", action="store_true", help="one q81/no-Huffman pass for a build smoke check")
    parser.add_argument("--no-simd", action="store_true", help="build smoke checks on hosts without an assembler; not the ARM benchmark")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or output == ROOT or output in ROOT.parents:
        parser.error("output must be a new directory outside protected ancestors")
    if not 3 <= args.rounds <= 101 or not 1 <= args.repeats <= 5:
        parser.error("rounds: 3..101; repeats: 1..5")
    output.mkdir(parents=True)
    simd = "OFF" if args.no_simd else "ON"
    binary, stock, builds = build(output, simd)
    affinity = sorted(os.sched_getaffinity(0))
    cpu = affinity[0]
    settings = [(81, 0)] if args.quick else [(q, h) for q in [35, 81, 95] for h in [0, 1]]
    repeats = 1 if args.quick else args.repeats
    cases = []
    for repeat in range(repeats):
        patterns = ["flat", "sparse", "text", "gradient", "noise", "photo"]
        if repeat % 2:
            patterns.reverse()
        for pattern in patterns:
            for quality, huffman in settings:
                command = ["nice", "-n", "10", "taskset", "-c", str(cpu), str(binary), pattern,
                           str(quality), str(huffman), str(args.rounds)]
                if pattern == "photo":
                    command.append(str(stock / "testimages/testorig.jpg"))
                result = run(command, capture_output=True, text=True)
                case = json.loads(result.stdout)
                case["repeat"] = repeat
                cases.append(case)
                baseline = case["native_stock"]["median_ms"]
                screen = case["native_screen"]["median_ms"]
                print(f"repeat={repeat} {pattern:8} q={quality:2} huff={huffman} native_ms={baseline:.3f}->{screen:.3f} reduction={100*(1-screen/baseline):.1f}%", flush=True)
    result = {"version": "3.2.1", "measured_utc": datetime.now(timezone.utc).isoformat(),
              "architecture": os.uname().machine,
              "lscpu": json.loads(subprocess.check_output(["lscpu", "-J"], text=True)),
              "compiler": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
              "cmake": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
              "affinity_cpu": cpu, "nice": 10, "simd": simd, "archive_sha256": ARCHIVE_SHA,
              "build": builds, "cases": cases,
              "scope": "Paired encode and native JPEG-to-JPEG processing; returned output comparison/free and Rust copy excluded. Synthetic 1080p JPEG source is stock q95 4:4:4; photo is public upstream testorig.jpg. No devices/services accessed."}
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Saved", output / "results.json", flush=True)


if __name__ == "__main__":
    main()

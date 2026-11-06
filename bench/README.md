# Current 3.2.1 comparison

This benchmark builds **unmodified libjpeg-turbo 3.2.1** and a separate copy
with the **exact production patch from `build.rs`**. Both have identical
Release, SIMD and accurate-integer settings. All defined library symbols and
the two native wrappers are prefixed so both implementations run in one
process without replacing any installed library.

Requirements: Python 3, CMake, a C compiler, GNU `nm`/`objcopy`, `nice`,
`taskset` and upstream SIMD prerequisites. ARM64 uses NEON; x86 SIMD builds
also require NASM. Build parallelism is two jobs.

```sh
python3 bench/run.py target/benchmark-3.2.1 --rounds 31 --repeats 3
```

The output directory must not already exist. The script never installs a
library, opens a capture device or changes a service. Results and generated
sources stay in that directory. The [manual GitHub workflow](../.github/workflows/benchmark.yml)
runs on a hosted ARM64 runner and saves the raw JSON as an artifact. The
2026-11-06 [results and analysis](../BENCHMARKS.md) also preserve the raw data
in the repository so it remains available after the artifact expires.

## Inputs and measurement

- Five deterministic 1920×1080 synthetic inputs: constant color, sparse glyphs,
  dense glyphs, gradients and seeded noise. They are first encoded by stock
  3.2.1 at q95/4:4:4; the resulting JPEG is the shared recompression input.
- One public upstream JPEG, `testimages/testorig.jpg` (227×149). It is a small
  photographic workload, not a 1080p screen or a private capture.
- Quality 35, 81 and 95, each with standard/optimized Huffman tables. Color
  pixel output is always 4:4:4 and uses `JDCT_ISLOW`.
- Four warm-up rounds, then 31 timed rounds per case. Stock/screen order
  alternates each round; three passes reverse workload order on the middle
  pass. The benchmark process is bound to one permitted CPU with nice 10.
- Every warm-up/timed pair must produce identical complete JPEG bytes.

Two scopes are reported separately:

1. **Encode:** stock decoding is done outside timing, then the same RGB buffer
   is encoded by both libraries. Encoder creation/destruction and output
   allocation are included.
2. **Native JPEG→JPEG:** the unchanged `native.c` is compiled against each
   library. The stock wrapper uses standard mode and the patched wrapper uses
   screen mode. Timing includes object setup, decode, pixel allocation,
   re-encoding and wrapper cleanup.

Returned-output comparison/freeing and the Rust `Vec` copy are outside timing.
There is no capture, networking or browser display. Raw records contain every
time sample, medians, nearest-rank P95, byte lengths, input identifiers,
compiler/CPU information and library/patch/binary hashes. Public CI hardware
results must not be presented as RK3399 throughput.

This measures the exact-flat **Screen** optimization. Coefficient-domain and
Huffman strategies are different output/quality tradeoffs and are not folded
into this byte-identical comparison.

## Build smoke check

On a development host without NASM:

```sh
python3 bench/run.py target/benchmark-smoke --quick --rounds 3 --no-simd
```

Both libraries disable SIMD in this mode, and the JSON explicitly records
`simd: OFF`. It checks compilation and paired output, not ARM performance.
Keep it separate from the SIMD-enabled measurement above.

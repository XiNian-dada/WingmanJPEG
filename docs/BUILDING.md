# Build and validation

## Requirements

- Rust 1.88 or newer (including Cargo).
- A C compiler, make, tar and CMake 3.15 or newer.
- A matching target C toolchain for cross builds. The `cc` and `cmake` Cargo
  build dependencies select the target compiler from the normal target settings.
- ARM builds require libjpeg-turbo's SIMD support. Non-ARM development builds
  disable upstream SIMD so NASM is optional; they are not optimal x86 builds.

The complete pinned libjpeg-turbo archive is in `vendor/`; no download of that
archive is needed during compilation. Cargo may fetch Rust dependencies on a
fresh machine. The build validates the archive hash, extracts into `OUT_DIR`,
applies the constant-block patch there and links the static library. It does
not change the system library or original archive. Native build parallelism is
capped at two jobs; `CARGO_BUILD_JOBS=2` also bounds Cargo's top-level jobs.

```sh
CARGO_BUILD_JOBS=2 cargo test --locked --release
cargo run --locked --release --example quickstart
cargo fmt --all -- --check
CARGO_BUILD_JOBS=2 cargo clippy --locked --release --all-targets -- -D warnings
```

## What the tests cover

`src/lib.rs` includes checks for:

- `Screen`/`Standard` full-byte equality for color/gray sources, grayscale
  output, odd and very small dimensions, six quality values and both Huffman
  settings.
- Decodable coefficient output and exact color preserve-mode decoded pixels.
- Truncated, invalid and oversized inputs returning errors, followed by a
  successful frame to check recovery.
- All 256 grayscale sample values at all 100 quality settings, independent
  concurrent calls, and EXIF/ICC marker behavior.

See the actual test functions for their finite fixtures and assertions. They
are not a proof for every possible JPEG. ARM64 tests execute the native NEON
path; x86 tests exercise the scalar fallback. Historical broader kernel tests
are linked separately in [BENCHMARKS.md](../BENCHMARKS.md).

The [CI workflow](../.github/workflows/ci.yml) runs release tests, the README
quickstart, formatting and strict Clippy on Linux x86_64 (Rust 1.88), Linux
ARM64 and macOS. Runner availability and actual job results are visible in
[GitHub Actions](https://github.com/XiNian-dada/WingmanJPEG/actions).

### Initial standalone validation, 2026-11-06

On Linux x86_64 with Rust 1.99.1, GCC 11.4.0 and CMake 3.22.1:

- Release compilation and all six unit tests passed.
- Strict release Clippy passed for all targets; formatting passed separately.
- The README quickstart returned `screen == standard: true` and 1,534 bytes.
- All four file-conversion modes succeeded on upstream's `testorig.jpg`;
  screen/standard files matched byte for byte. Attempting to reuse an output
  path returned an error and left the original file hash unchanged.
- `validate_frame` passed all 48 native operations on the same 227×149 image.

These are correctness and build checks. The Linux ARM64, macOS and Linux
Rust 1.88 CI jobs also [passed](https://github.com/XiNian-dada/WingmanJPEG/actions/runs/42217895014).
The separate SIMD-enabled, stock-versus-patched 3.2.1 performance comparison
is recorded in [BENCHMARKS.md](../BENCHMARKS.md).

## Check a JPEG without capture hardware

```sh
cargo run --locked --release --example validate_frame -- /absolute/path/frame.jpg
```

This reads a file, compares `Screen`/`Standard`, decodes coefficient outputs
and checks preserve-mode pixels at multiple settings. It does not open a
camera or configure any device. Inputs remain local unless you separately
choose to share them.

## When a build or frame fails

**CMake, make or tar is missing:** provide the listed build tools; no system
libjpeg development package is needed because the headers are bundled.

**The source hash or patch location does not match:** stop and check the source
archive and pinned build script. Do not bypass the hash or loosen patch checks
to build an unknown upstream version.

**`Preserve` rejects the frame:** it accepts common 8-bit YCbCr/gray JPEGs with
baseline quantizers. Choose `Standard` for a pixel route, or `Coefficient` for
its documented fallback. Pixel encoding can change the reconstructed image.

**A frame exceeds dimensions, scans, output or time limits:** handle the
returned error and skip/report that frame. Increasing process concurrency does
not relax per-frame limits and increases total memory use.

**The output JPEG is larger:** re-encoding can increase size, especially when
moving to 4:4:4. Compare the actual bytes and chosen mode; a quality slider is
not a universal bandwidth target.

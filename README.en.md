# WingmanJPEG

**Reduce JPEG re-encoding work for terminal and desktop frames.** A Rust API over bundled libjpeg-turbo 3.2.1 with exact constant-block acceleration, coefficient-domain recompression and source-quality-preserving Huffman optimization.

[中文](README.md) · [Quickstart](#quickstart) · [Choose a mode](#choose-a-mode) · [Algorithms](docs/ALGORITHMS.md) · [Build and validation](docs/BUILDING.md)

Extracted from the capture-frame processor in [WingmanKVM](https://github.com/XiNian-dada/WingmanKVM). Takes existing JPEG/MJPEG frames and returns standard JPEGs. Available as Git source; not published to crates.io.

## Quickstart

Requirements: **Rust 1.88+**, a C compiler, make, tar and **CMake 3.15+**. No capture hardware or input image is needed for this example:

```sh
git clone https://github.com/XiNian-dada/WingmanJPEG.git
cd WingmanJPEG
cargo run --locked --release --example quickstart
```

It generates a synthetic 256×128 screen in memory, re-encodes it with `Screen` and `Standard`, and asserts that their complete JPEG bytes match. An actual Linux x86_64 run printed:

```text
decoded dimensions: 256x128
screen == standard: true
encoded bytes: 1534
```

Convert your own JPEG into a new output file:

```sh
cargo run --locked --release --example transcode -- input.jpg output.jpg screen 81
```

The example uses color and standard Huffman tables, and refuses to overwrite existing files. The last two arguments are optional and default to `screen 81`. Use the Rust API to select grayscale or Huffman optimization.

## Use from Rust

Add a Git dependency to your application's `Cargo.toml`; pin a verified commit for reproducible builds:

```toml
[dependencies]
wingman-jpeg-native = { git = "https://github.com/XiNian-dada/WingmanJPEG.git" }
```

```rust
use wingman_jpeg_native::{transcode_into, Mode, Options};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let input = std::fs::read("input.jpg")?;
    let mut output = Vec::new();
    transcode_into(
        &input,
        Options {
            quality: 81,
            grayscale: false,
            mode: Mode::Screen,
            optimize_huffman: false,
        },
        &mut output,
    )?;
    println!("{} bytes -> {} bytes", input.len(), output.len());
    Ok(())
}
```

`transcode_into` runs synchronously and clears `output` on entry and on failure. Native memory is allocated and freed in C, then copied into the Rust `Vec` on success. Reusing the caller's vector retains its capacity; native codec objects and buffers are still created per frame.

## Choose a mode

| Mode | Use it for | Behavior |
| --- | --- | --- |
| `Screen` | Terminals, menus and screens with solid backgrounds | Skips DCT/quantization for exactly constant blocks. JPEG bytes match `Standard` with identical options. |
| `Standard` | General recompression and a reference path | libjpeg-turbo's accurate integer decoder and encoder. |
| `Coefficient` | Reducing existing JPEGs without a pixel round trip | Requantizes DCT coefficients, only coarsens precision, retains source sampling. Unsupported formats fall back to `Screen`. |
| `Preserve` | Trying to save bandwidth without additional color-image loss | Keeps source coefficients and quantizers, optimizes Huffman tables, ignores quality. Unsupported formats return an error. |

- `grayscale: true` keeps JPEG luma, removing color. It can differ from RGB-derived grayscale.
- `optimize_huffman: true` adds a statistics pass without changing that path's reconstructed image. `Preserve` always enables it.
- Color pixel encoders output **4:4:4**; coefficient modes keep source sampling. Equal quality numbers do not imply equal bitrate or image quality across modes.
- `Preserve` retains EXIF and ICC; grayscale drops color ICC markers. Other modes do not preserve image metadata.

When the source JPEG already meets your bandwidth and quality needs, forwarding its bytes usually saves the most CPU. `Screen` saves computation while keeping the reference encoder's output size. Huffman savings depend on frame content and the source tables.

## How the acceleration works

An exactly constant 8×8 component block has one DC coefficient and 63 zero AC coefficients. `Screen` checks every sample and fills the coefficients from a precomputed table when the block is constant; otherwise it uses the original path. ARM64 uses NEON for detection. Adaptive probing suspends detection when few blocks qualify.

Near-constant blocks are not treated as constant. The shortcut retains the reference path's integer quantization and rounding. See [ALGORITHMS.md](docs/ALGORITHMS.md) for formulas and coefficient-domain kernels.

### Current 3.2.1 comparison

Measured on 2026-11-06 using a GitHub-hosted ARM64 runner (Neoverse-N2): **unmodified libjpeg-turbo 3.2.1** versus **3.2.1 with the current production patch**. Both use GCC 13.3, Release, NEON, quality 81, 4:4:4 and standard Huffman tables. Native JPEG-to-JPEG medians for synthetic 1080p inputs:

| Frame | Stock 3.2.1 | Screen | Time reduction |
| --- | ---: | ---: | ---: |
| Constant color | 15.652 ms | 11.341 ms | 27.5% |
| Sparse glyphs | 17.653 ms | 13.770 ms | 22.0% |
| Dense glyphs | 30.431 ms | 29.281 ms | 3.8% |
| Gradient | 20.134 ms | 20.188 ms | -0.3% |
| Noise | 64.565 ms | 63.465 ms | 1.7% |

**Encode time** is 42.1% lower for constant color and 36.2% lower for sparse glyphs. Each case has four warm-up rounds, 31 timed rounds and three passes; the table pools 93 samples per path. Every output pair is byte-identical. The small upstream photo is about 1.1% slower. Noise's small median improvement does not extend to P95, so it is not evidence of a general speedup.

The current patch helps most when many blocks are constant; complex content has smaller gains or slight regressions. These are **cloud ARM64/synthetic-input** measurements, not RK3399 or real desktop throughput. Timing includes native decode, pixel allocation, encode and object management; returned-buffer freeing, Rust output copying, capture, networking and display are excluded. [Full methodology, parameter matrix and raw data](BENCHMARKS.md).

## Build and processing boundaries

- The current validation setup targets Linux/macOS. ARM builds require upstream SIMD. Other development targets build without NASM by disabling upstream SIMD; this is not a measure of optimal x86 performance. No Windows validation setup is provided.
- libjpeg-turbo is linked statically and does not replace a system library. Its original source archive is included and SHA-256 checked before a small opt-in patch is applied in the private build directory.
- Limits: **32 MiB** input, **64 MiB** output, **8192** pixels per edge, **33,177,600** total pixels, 8-bit input precision and 64 input scans.
- Each JPEG object's virtual-array budget is 256 MiB. Progress callbacks check an approximately 3–4 second processing budget. These are not a process-wide memory quota or a hard real-time guarantee.
- Coefficient modes support common 8-bit YCbCr/grayscale JPEGs with quantizers in 1–255. Invalid, corrupt, truncated or oversized frames return errors for the caller to handle.
- This is a capture-frame processor. Check the boundaries before using it for photo archival, full metadata preservation or broad JPEG-format support.

## Validate and contribute

```sh
cargo fmt --all -- --check
cargo test --locked --release
cargo run --locked --release --example quickstart
cargo clippy --locked --release --all-targets -- -D warnings
```

Tests cover screen/standard byte parity across color, grayscale, odd dimensions, quality and Huffman settings; preserve-mode decoded-pixel parity; corrupt-frame recovery; metadata and size limits. NEON must be tested on ARM64; other targets do not execute it. See [BUILDING.md](docs/BUILDING.md).

For [bug reports](https://github.com/XiNian-dada/WingmanJPEG/issues), include the mode, options, platform, toolchains and a minimal input you can share publicly. Check that the image contains no private information.

## License and origin

Original wrapper and optimization code: [MIT](LICENSE). libjpeg-turbo retains its IJG, BSD and zlib licenses; original source and notices are in [vendor/](vendor/). [THIRD_PARTY.md](THIRD_PARTY.md) records provenance, the pinned hash and the patch scope. JPEG, Huffman optimization and coefficient transcoding are established algorithms/upstream capabilities; this project's additions are the exact constant-block shortcut, exact NEON requantization and bounded recoverable error handling.

This software is based in part on the work of the Independent JPEG Group.

# Performance evidence

## Current libjpeg-turbo 3.2.1, 2026-11-06

The exact-flat Screen patch reduces native JPEG-to-JPEG time by **27.5% on
constant color, 22.0% on sparse glyphs and 3.8% on dense glyphs** at the default
q81/standard-Huffman working point in this measurement. All compared JPEGs are
byte-identical. Benefits are workload-dependent; this is not a general JPEG
speedup or a new compression format.

### Build and method

- GitHub-hosted `ubuntu-24.04-arm`, ARM Neoverse-N2, one of four permitted CPUs,
  nice 10. This is cloud hardware, not RK3399. No capture/production machine
  was used for these measurements.
- GCC 13.3.0, CMake 3.31.6, `-O3 -DNDEBUG`, SIMD/NEON **enabled in both libraries**.
  Baseline is the unmodified bundled 3.2.1 archive. The comparison copy applies
  the exact replacement strings from production `build.rs` and the same
  `flat_core.h`. Both libraries/wrappers are symbol-prefixed in one process.
- Five deterministic 1920×1080 synthetic screens are first encoded by stock
  3.2.1 at q95/4:4:4. This common source JPEG is decoded/re-encoded for each
  path. A sixth input is upstream's public `testorig.jpg`, 227×149; it is not
  comparable in absolute latency to the 1080p workloads.
- Quality 35/81/95 × standard/optimized Huffman × six inputs × three passes:
  **108 cases, 13,392 timed calls and 7,560 byte-identical pairs** including
  warm-ups. Four warm-up rounds and 31 timed rounds per case. Stock/screen order
  alternates per round; the middle pass reverses workload order.
- Tables pool 93 timed samples per path/settings. Medians are calculated from
  those samples; P95 is nearest-rank. Reduction is `1 - screen / stock`; a
  negative value means slower. Timing excludes returned-output comparison/free,
  the Rust `Vec` copy, file I/O, capture, networking and display.

The native scope includes the unchanged C wrapper's setup, decoding, pixel
allocation, encoding and internal cleanup. Encode-only uses the same decoded
RGB buffer, with decoder work outside timing, and includes encoder object
creation/destruction plus output allocation.

### Default q81, standard Huffman

| Input | Encode stock → screen | Native stock → screen | Native time reduction |
| --- | ---: | ---: | ---: |
| Constant color | 10.254 → 5.933 ms | 15.652 → 11.341 ms | 27.5% |
| Sparse glyphs | 10.745 → 6.859 ms | 17.653 → 13.770 ms | 22.0% |
| Dense glyphs | 13.781 → 12.670 ms | 30.431 → 29.281 ms | 3.8% |
| Gradient | 11.521 → 11.539 ms | 20.134 → 20.188 ms | -0.3% |
| Noise | 25.310 → 24.610 ms | 64.565 → 63.465 ms | 1.7% |
| Upstream photo (227×149) | 0.222 → 0.226 ms | 0.362 → 0.366 ms | -1.1% |

### Default native P95

| Input | Stock P95 | Screen P95 |
| --- | ---: | ---: |
| Constant color | 15.767 ms | 11.414 ms |
| Sparse glyphs | 17.737 ms | 13.844 ms |
| Dense glyphs | 30.500 ms | 29.391 ms |
| Gradient | 20.271 ms | 20.261 ms |
| Noise | 64.929 ms | 65.241 ms |
| Upstream photo (227×149) | 0.372 ms | 0.378 ms |

The gradient is essentially unchanged at this working point; the small photo
has roughly 4 microseconds of additional native latency. Noise's median gain
is small and its P95 is slightly worse. These results do not establish a
universal advantage on complex content. Small differences can also reflect
cloud scheduling, caches or generated-code layout; the large constant/sparse
reductions are the useful result. No formal statistical confidence interval
or real desktop/video distribution is claimed.

### Native reduction across all settings

Each cell compares stock/screen at that row's identical quality, sampling and
entropy options; output bytes match. It does not compare image quality or
bitrate between different rows.

| Quality | Huffman | Constant | Sparse | Dense | Gradient | Noise | Photo |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 35 | standard | 27.5% | 22.2% | 3.9% | -0.5% | -0.2% | -1.0% |
| 35 | optimized | 22.6% | 29.5% | 15.1% | 2.5% | 5.7% | -0.6% |
| 81 | standard | 27.5% | 22.0% | 3.8% | -0.3% | 1.7% | -1.1% |
| 81 | optimized | 22.4% | 28.4% | 14.7% | 16.5% | 4.9% | -0.2% |
| 95 | standard | 27.7% | 21.7% | 3.3% | -0.5% | 1.8% | -1.0% |
| 95 | optimized | 23.0% | 28.5% | 13.4% | 15.1% | 4.4% | -0.0% |

### Reproduce and inspect

```sh
python3 bench/run.py target/benchmark-3.2.1 --rounds 31 --repeats 3
```

See [bench/README.md](bench/README.md) for prerequisites, fixture construction
and scope. Raw samples and all source/library/binary hashes are preserved in
[results.json](bench/results/arm64-2026-11-06/results.json).

- [Successful measured workflow run](https://github.com/XiNian-dada/WingmanJPEG/actions/runs/42219111943)
- [Measured source commit](https://github.com/XiNian-dada/WingmanJPEG/tree/c563365f2b91b08f64ec40a33ae92de843ad9cb7)
- Raw JSON SHA-256: `70c77c1d240dfe72d7247bcb0e61096cb7eaf494531b41c92877dc5bc17d7994`.

The historical results below use 2.1.5 on different hardware/inputs and must
not be combined with current cloud measurements. Current results measure only
the Screen optimization; coefficient and entropy strategies are separate
output/quality tradeoffs.

## Historical 2.1.5 RK3399 TTY frames, 2026-11-03

Real PVE Linux TTY output, captured as 1920×1080, 8-bit YCbCr 4:2:2 MJPEG.
The reference and patched pixel paths re-encode at quality 81 with 4:4:4 output.
Both C libraries are libjpeg-turbo 2.1.5 with identical Release/SIMD builds.
Measurements use one RK3399 big core, two warm-up rounds and 21 timed rounds;
the background WingmanKVM service continues running. Full-frame timings
include decoding, encoding and object creation/destruction.

| Frame | Standard median | Exact-flat median | Reduction |
| --- | ---: | ---: | ---: |
| Sparse TTY | 54.94 ms | 37.90 ms | 31.0% |
| Monochrome listing | 61.22 ms | 46.28 ms | 24.4% |
| Color listing | 66.06 ms | 54.31 ms | 17.8% |

All compared JPEG outputs match byte for byte. Samples contain six distinct
frames across three mostly static workloads, not a broad desktop/video corpus.
These timings exclude device waits, network, TLS and browser display. No real
graphical desktop workload or end-to-end latency improvement was measured.

- [Full methodology and quality comparison](https://github.com/XiNian-dada/WingmanKVM/blob/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4/components/wingman-jpeg/turbo-flat-lab/REAL_BENCHMARKS.md)
- [Raw timings and binary hashes](https://github.com/XiNian-dada/WingmanKVM/blob/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4/components/wingman-jpeg/turbo-flat-lab/benchmarks/rk3399-real-tty.json)
- [Quality measurements](https://github.com/XiNian-dada/WingmanKVM/blob/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4/components/wingman-jpeg/turbo-flat-lab/benchmarks/rk3399-real-quality.json)

## Coefficient-domain and Huffman results

Historical near-matched-bitrate TTY comparisons show 31–50% lower full-frame
time for the coefficient route at selected working points. It produces a
different JPEG from pixel re-encoding; text PSNR and SSIM are generally close
and do not show an across-the-board quality win. See the full report above.

Retaining all source quantizers/coefficients and optimizing Huffman tables
reduced the sparse TTY from 53,084 to 30,478 bytes (42.6%). Monochrome and color
listings saved 6.1% and 4.4%, respectively. All three decoded images matched
their source images; processing took approximately 30–52 ms. These are
content-dependent bandwidth savings with extra processing cost.

The exact NEON requantization kernel's research tests cover 50,135,040
coefficient cases across all signed 16-bit values, 255 divisors and three
representative old quantizers, plus mixed tables and unaligned blocks. This
does not exhaust every possible old/new quantizer pair.

- [Coefficient formula, synthetic benchmarks and ARM validation](https://github.com/XiNian-dada/WingmanKVM/blob/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4/components/wingman-jpeg/turbo-flat-lab/REQUANT.md)
- [Raw coefficient benchmark and test log](https://github.com/XiNian-dada/WingmanKVM/blob/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4/components/wingman-jpeg/turbo-flat-lab/benchmarks/rk3399-requant.json)

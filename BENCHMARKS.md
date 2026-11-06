# Performance evidence

## Status

This repository bundles libjpeg-turbo **3.2.1**. The numbers below come from
earlier **2.1.5** experiments in WingmanKVM; they establish the usefulness of
the optimization on those workloads, not the speed of this standalone build.
No new performance benchmark is claimed for version 0.1.0.

The original records are pinned to the public research commit
[`19005bff9e38f6d45d17ce4a22e6fcce08f24ee4`](https://github.com/XiNian-dada/WingmanKVM/tree/19005bff9e38f6d45d17ce4a22e6fcce08f24ee4).

## Real RK3399 TTY frames, 2026-11-03

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

## Measuring the current component

Use the same input frame, quality, grayscale and Huffman options when comparing
`Screen` and `Standard`. Warm up before timing and alternate execution order;
check the complete JPEG bytes, not only dimensions. Record compiler versions,
architecture, SIMD settings, workload and both median and tail latency.

`examples/validate_frame.rs` checks current native parity on a supplied JPEG
and prints single-call timings for coefficient modes. Those single-call
timings are smoke-test information, not a warmed multi-round benchmark.
For cross-mode quality comparisons, match actual byte rate or reconstructed
quality rather than assuming equal quality values are equivalent.

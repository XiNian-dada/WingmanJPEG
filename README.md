# WingmanJPEG

**为终端和桌面画面减少 JPEG 重编码开销。** 提供 Rust API，在内嵌的 libjpeg-turbo 3.2.1 上识别完全纯色的 8×8 分量块，直接生成与标准编码相同的系数；也支持系数域重压缩和保留源画质的 Huffman 优化。

[English](README.en.md) · [快速开始](#快速开始) · [选择处理方式](#选择处理方式) · [原理与性能](docs/ALGORITHMS.md) · [构建与验证](docs/BUILDING.md)

从 [WingmanKVM](https://github.com/XiNian-dada/WingmanKVM) 的采集帧处理组件独立而来。适合在应用中处理已有 JPEG/MJPEG 帧；输入和输出都是标准 JPEG。当前以 Git 源码提供，尚未发布到 crates.io。

## 快速开始

需要 Rust **1.88+**、C 编译器、make、tar 和 **CMake 3.15+**。先用不需要采集卡或图片文件的示例跑通：

```sh
git clone https://github.com/XiNian-dada/WingmanJPEG.git
cd WingmanJPEG
cargo run --locked --release --example quickstart
```

示例在内存中生成一张 256×128 的合成屏幕图，分别使用 `Screen` 和 `Standard` 重编码，并断言完整 JPEG 字节相同。Linux x86_64 上的一次实际输出：

```text
decoded dimensions: 256x128
screen == standard: true
encoded bytes: 1534
```

处理自己的 JPEG（输出路径必须尚不存在）：

```sh
cargo run --locked --release --example transcode -- input.jpg output.jpg screen 81
```

这个文件转换示例使用彩色、标准 Huffman 表；最后两个参数可省略，默认 `screen 81`。黑白和 Huffman 开关通过下面的 Rust API 设置。

## 接入 Rust 项目

在项目的 `Cargo.toml` 中添加 Git 依赖；需要可复现构建时，固定经过验证的 commit：

```toml
[dependencies]
wingman-jpeg-native = { git = "https://github.com/XiNian-dada/WingmanJPEG.git" }
```

完整调用：

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

`transcode_into` 同步执行，调用开始时清空 `output`；失败时输出保持为空并返回错误。原生缓冲由 C 分配和释放，成功后复制进 Rust 的 `Vec`。可复用调用方 `Vec` 的容量，但当前仍会逐帧创建原生编解码对象和缓冲。

## 选择处理方式

| 模式 | 适合的需求 | 画质与行为 |
| --- | --- | --- |
| `Screen` | 终端、菜单、存在大片纯色背景的桌面 | 纯色块跳过 DCT/量化；同参数下与 `Standard` 的 JPEG 字节相同。 |
| `Standard` | 常规重编码、性能和正确性对照 | libjpeg-turbo 的准确整数编解码路径。 |
| `Coefficient` | 已有 JPEG，需要降低体积并减少像素往返 | 直接重新量化 DCT 系数，只降低精度；保留源采样。不支持的格式回退 `Screen`。 |
| `Preserve` | 已有 JPEG，希望在不增加二次画质损失的情况下尝试省流 | 保留源系数及量化表，优化 Huffman；忽略质量值。不支持的格式返回错误。 |

- `grayscale: true` 只保留 JPEG 亮度，所有模式均可选；会删除颜色，可能与“RGB 转灰度”得到不同亮度。
- `optimize_huffman: true` 统计当前帧并生成编码表，不改变所选路径的重建画面，但增加统计成本。`Preserve` 始终启用。
- `Screen`/`Standard` 的彩色输出为 **4:4:4**；系数模式保留源采样。相同质量数值不代表不同模式具有相同码率或画质。
- `Preserve` 保留 EXIF 和 ICC；黑白模式移除颜色 ICC。其他模式不保留图像元数据。

输入已经是合适的 JPEG、带宽也可接受时，直接发送源字节通常最省 CPU。`Screen` 的主要目标是减少计算，文件大小与标准重编码相同；`Preserve` 是否明显省流取决于画面和源编码表。

## 为什么纯色块可以更快

一个完全同色的 8×8 分量块，DCT 后只有 DC 系数，63 个 AC 系数为零。`Screen` 检查全部 64 个采样，命中后用预计算表填写系数；其他块走原有实现。ARM64 用 NEON 检查，自适应采样在命中率低时暂停检查，减少纹理和噪声画面的额外开销。

这个快捷路径不把“接近纯色”的块当成纯色，也不改变标准编码的量化和舍入规则。[算法说明](docs/ALGORITHMS.md) 给出公式、适用条件与系数域优化方法。

### 与原版 3.2.1 的实测对照

2026-11-06，在 GitHub ARM64 云端 runner（Neoverse-N2）上，对照**未修改的 libjpeg-turbo 3.2.1**与**应用当前生产补丁的 3.2.1**。两边均为相同 GCC 13.3、Release、NEON、质量 81、4:4:4、标准 Huffman；下面是 1080p 合成 JPEG 的原生整帧重压缩中位数：

| 画面 | 原版 3.2.1 | 屏幕优化 | 耗时减少 |
| --- | ---: | ---: | ---: |
| 纯色 | 15.652 ms | 11.341 ms | 27.5% |
| 稀疏文字 | 17.653 ms | 13.770 ms | 22.0% |
| 密集文字 | 30.431 ms | 29.281 ms | 3.8% |
| 渐变 | 20.134 ms | 20.188 ms | -0.3% |
| 噪声 | 64.565 ms | 63.465 ms | 1.7% |

纯色、稀疏文字的**编码阶段**分别减少 42.1%、36.2%。每项预热 4 轮、计时 31 轮、重复 3 次，上表合并 93 次计时取中位数；全部输出逐字节一致。另测上游小照片，约慢 1.1%。噪声的微小中位数收益没有对应更好的 P95，不能作为普遍加速的依据。

这说明当前优化在纯色块较多的画面上有明确收益，复杂内容收益较小或略有回退。结果来自**云端 ARM64 与合成输入**，不代表 RK3399 或真实桌面吞吐。计时包含原生解码、像素分配、编码和对象管理，不含返回缓冲释放、Rust 输出复制、采集、网络或显示。[完整方法、所有参数与原始数据](BENCHMARKS.md)。

## 构建前需要知道

- 当前验证入口面向 Linux/macOS；ARM 构建要求上游 SIMD。其他开发架构可不安装 NASM，此时构建会关闭上游 SIMD，不能拿它评价最佳 x86 性能。Windows 暂未提供验证流程。
- 库以静态方式编入应用，不替换系统 libjpeg。原始源码压缩包随仓库保存，构建时先核对 SHA-256，再只修改私有构建目录中的 `jcdctmgr.c`。
- 每帧输入不超过 **32 MiB**，输出不超过 **64 MiB**；单边不超过 **8192** 像素，总像素不超过 **33,177,600**，输入精度为 8 位。
- 渐进扫描最多 64 次；每个 JPEG 对象的虚拟数组预算为 256 MiB。进度回调检查约 3–4 秒的处理时间预算。这些限制不是进程总内存上限或硬实时保证。
- 系数模式接受常见 8 位 YCbCr/灰度 JPEG 和 1–255 的量化表。损坏、截断或超限帧返回错误，应用决定跳过还是报告。
- 面向采集帧处理；需要归档照片、保留所有元数据或处理广泛 JPEG 变体时，应核对这些限制。

## 验证与贡献

```sh
cargo fmt --all -- --check
cargo test --locked --release
cargo run --locked --release --example quickstart
cargo clippy --locked --release --all-targets -- -D warnings
```

测试覆盖彩色/灰度、奇数尺寸、不同质量与 Huffman 设置的 `Screen`/`Standard` 字节一致性，`Preserve` 的解码像素一致性，以及坏帧错误恢复、元数据和尺寸限制。ARM64 NEON 的验证必须在 ARM64 上运行；其他架构的测试不会执行它。[构建说明](docs/BUILDING.md) 包含更完整的验证范围。

反馈时请提供模式、选项、平台、Rust/C 工具链版本和可公开的最小输入；请先确认图片中没有私密信息。[提交问题](https://github.com/XiNian-dada/WingmanJPEG/issues)。

## 许可与来源

自有封装和优化代码采用 [MIT](LICENSE)。libjpeg-turbo 保留其 IJG、BSD 和 zlib 许可，完整源码和声明在 [vendor/](vendor/)；来源、固定哈希及修改范围见 [THIRD_PARTY.md](THIRD_PARTY.md)。标准 JPEG、Huffman 优化和系数域转码来自既有算法和上游能力；本项目的工作是纯色块快捷路径、精确 NEON 重新量化及有界错误恢复封装。

This software is based in part on the work of the Independent JPEG Group.

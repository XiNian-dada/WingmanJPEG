//! Generate a small synthetic screen and check exact screen/standard parity.
use image::{ExtendedColorType, ImageEncoder, Rgb, RgbImage};
use wingman_jpeg_native::{transcode_into, Mode, Options};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pixels = RgbImage::from_fn(256, 128, |x, y| {
        if (16..112).contains(&x) && (16..24).contains(&y) {
            Rgb([220, 220, 220])
        } else if (16..80).contains(&x) && (40..48).contains(&y) {
            Rgb([80, 200, 120])
        } else {
            Rgb([16, 16, 16])
        }
    });
    let mut input = Vec::new();
    image::codecs::jpeg::JpegEncoder::new_with_quality(&mut input, 95).write_image(
        pixels.as_raw(),
        pixels.width(),
        pixels.height(),
        ExtendedColorType::Rgb8,
    )?;
    let options = Options {
        quality: 81,
        grayscale: false,
        mode: Mode::Screen,
        optimize_huffman: false,
    };
    let mut screen = Vec::new();
    let mut standard = Vec::new();
    transcode_into(&input, options, &mut screen)?;
    transcode_into(
        &input,
        Options {
            mode: Mode::Standard,
            ..options
        },
        &mut standard,
    )?;
    assert_eq!(screen, standard, "screen differs from standard encoding");
    let decoded = image::load_from_memory(&screen)?;
    assert_eq!((decoded.width(), decoded.height()), (256, 128));
    println!(
        "decoded dimensions: {}x{}",
        decoded.width(),
        decoded.height()
    );
    println!("screen == standard: {}", screen == standard);
    println!("encoded bytes: {}", screen.len());
    Ok(())
}

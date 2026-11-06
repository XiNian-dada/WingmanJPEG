//! Convert one JPEG. Existing output files are never overwritten.
use std::{env, fs, fs::OpenOptions, io::Write};
use wingman_jpeg_native::{transcode_into, Mode, Options};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args_os().skip(1).collect();
    if !(2..=4).contains(&args.len()) {
        return Err("usage: transcode INPUT.jpg OUTPUT.jpg [screen|standard|coefficient|preserve] [QUALITY=81]".into());
    }
    let mode = match args.get(2).and_then(|s| s.to_str()).unwrap_or("screen") {
        "screen" => Mode::Screen,
        "standard" => Mode::Standard,
        "coefficient" => Mode::Coefficient,
        "preserve" => Mode::Preserve,
        _ => return Err("unknown mode; use screen, standard, coefficient or preserve".into()),
    };
    let quality = args
        .get(3)
        .map(|s| s.to_str().ok_or("quality must be a number from 1 to 100"))
        .transpose()?
        .unwrap_or("81")
        .parse::<u8>()?;
    if !(1..=100).contains(&quality) {
        return Err("quality must be from 1 to 100".into());
    }
    let input = fs::read(&args[0])?;
    let mut output = Vec::new();
    transcode_into(
        &input,
        Options {
            quality,
            grayscale: false,
            mode,
            optimize_huffman: false,
        },
        &mut output,
    )?;
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[1])?;
    file.write_all(&output)?;
    println!(
        "mode={mode:?} input_bytes={} output_bytes={}",
        input.len(),
        output.len()
    );
    Ok(())
}

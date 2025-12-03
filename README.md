# PNG to JPG Converter

A **fully functional PNG to JPEG converter written in raw C++** with **zero external dependencies**. No libpng, no libjpeg, no zlib - everything is implemented from scratch.

## Features

- **Complete PNG Decoder**
  - PNG file format parsing (chunks, signatures, CRC validation)
  - Zlib/Deflate decompression (fixed and dynamic Huffman codes)
  - PNG filter reconstruction (None, Sub, Up, Average, Paeth)
  - Multiple color type support:
    - Grayscale (type 0)
    - RGB (type 2)
    - Indexed/Palette (type 3)
    - Grayscale + Alpha (type 4)
    - RGBA (type 6)

- **Complete JPEG Encoder**
  - RGB to YCbCr color space conversion
  - 8x8 block-based DCT (Discrete Cosine Transform)
  - Quality-adjustable quantization (1-100)
  - Huffman entropy coding with standard JFIF tables
  - Proper JPEG file structure (SOI, APP0, DQT, SOF0, DHT, SOS, EOI markers)
  - Byte stuffing for 0xFF values

- **No External Libraries** - Pure C++ standard library only

## Usage

```bash
./converter <input.png> <output.jpg> [quality]
```

**Parameters:**
- `input.png` - Source PNG file
- `output.jpg` - Output JPEG file  
- `quality` - Optional, 1-100 (default: 85)

**Examples:**
```bash
# Basic conversion with default quality (85)
./converter photo.png photo.jpg

# High quality output
./converter image.png image_hq.jpg 95

# Smaller file size, lower quality
./converter screenshot.png screenshot.jpg 60
```

## Compilation

### macOS

```bash
# Using clang++ (default on macOS)
g++ -O2 -std=c++11 -o converter converter.cpp

# Or explicitly with clang
clang++ -O2 -std=c++11 -o converter converter.cpp
```

### Linux

```bash
g++ -O2 -std=c++11 -o converter converter.cpp
```

### Windows

**Using MinGW (GCC for Windows):**
```cmd
g++ -O2 -std=c++11 -o converter.exe converter.cpp
```

**Using Microsoft Visual Studio (Developer Command Prompt):**
```cmd
cl /O2 /EHsc /std:c++14 converter.cpp /Fe:converter.exe
```

**Using Visual Studio IDE:**
1. Create new "Console App" project
2. Add `converter.cpp` to Source Files
3. Set C++ Language Standard to C++11 or higher (Project Properties → C/C++ → Language)
4. Build → Build Solution (Ctrl+Shift+B)

**Using MSYS2/MinGW-w64:**
```bash
pacman -S mingw-w64-x86_64-gcc  # Install if needed
g++ -O2 -std=c++11 -o converter.exe converter.cpp
```

## Quality Guide

| Quality | Use Case | File Size |
|---------|----------|-----------|
| 30-50 | Thumbnails, previews | Smallest |
| 60-75 | Web images, general use | Small |
| 80-90 | High quality photos | Medium |
| 95-100 | Archival, professional | Large |

## Technical Details

### PNG Decoding Pipeline
1. Validate PNG signature (8 bytes)
2. Parse IHDR chunk (image dimensions, color type, bit depth)
3. Collect IDAT chunks (compressed image data)
4. Strip zlib header/footer, decompress with Deflate
5. Apply reverse PNG filters to reconstruct raw pixels
6. Convert to RGB format

### JPEG Encoding Pipeline
1. Convert RGB to YCbCr color space
2. Process image in 8×8 pixel blocks
3. Apply forward DCT to each block
4. Quantize DCT coefficients (quality-dependent)
5. Reorder coefficients in zigzag pattern
6. Encode DC coefficients (differential) and AC coefficients (run-length)
7. Apply Huffman coding
8. Write JFIF-compliant file structure

## Limitations

- Input must be valid PNG files
- 8-bit color depth only (most common)
- No interlaced PNG support
- No progressive JPEG output
- No EXIF metadata preservation

## License

This project is **open source** and free to use, modify, and distribute.

**Attribution Required:** If you use this code in your project, please credit the original author.

Example attribution:
```
PNG to JPG Converter by BitBoy14
https://github.com/BitBoy14/png-jpg-converter
```

## Author

**BitBoy14** - Built as a demonstration of image format internals without relying on external libraries.


## Tools

- Generator (PNG/SVG → C array):
```bash
python3 /home/ab/Coding/ESP32-S3-BOX/VoiceRecorder/tools/lvgl_icon_generator/generator_simple.py
```

- Viewer (preview icons on desktop):
```bash
python3 /home/ab/Coding/ESP32-S3-BOX/VoiceRecorder/tools/lvgl_image_viewer/viewer.py
```

Both are verified with the format below.

## What factory_demo does (and what we use)

1. Main menu icons are C files with an embedded byte array and an `lv_img_dsc_t` descriptor.
2. Factory icons sometimes include multiple formats; platform picks what it supports.
3. For ESP32‑S3‑BOX‑3 in this app we standardize on:
   - `LV_COLOR_DEPTH = 16` (RGB565)
   - `LV_COLOR_16_SWAP = 1` (little‑endian 16‑bit color ordering)
   - `LV_IMG_CF_TRUE_COLOR_ALPHA` (color + 8‑bit alpha)
   - Icon dimensions: `76 x 76`
4. Transparency is controlled by alpha. Background transparency = alpha `0x00`. It is not “white means transparent”.
5. The custom generator and viewer were built to debug issues and now match these settings.

## Pixel layout (TRUE_COLOR_ALPHA, RGB565, SWAP on)

Each pixel = 3 bytes:

- Byte 0: `color_lo` (RGB565 low byte)
- Byte 1: `color_hi` (RGB565 high byte)
- Byte 2: `alpha` (`0x00` transparent .. `0xFF` opaque)

Because `LV_COLOR_16_SWAP = 1`, bytes must be in the order `lo, hi` for the 16‑bit color.

Example (opaque red, RGB565 = 0xF800): `00 F8 FF`.

## LVGL descriptor checklist

```c
const lv_img_dsc_t icon_recorder = {
    .header.always_zero = 0,
    .header.w = 76,
    .header.h = 76,
    .data_size = sizeof(icon_recorder_map), // must be w*h*3
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = icon_recorder_map,
};
```

Project config must match:

- `LV_COLOR_DEPTH = 16`
- `LV_COLOR_16_SWAP = 1`
- Icons encoded as `LV_IMG_CF_TRUE_COLOR_ALPHA`

## Recommended colors

- Foreground: dark red (same hue used by `icon_network.c`) with alpha `0xFF`.
- Background: alpha `0x00`.

## Quick checklist for a new icon

1. Create a 76x76 image with transparent background.
2. Export as PNG with alpha preserved.
3. Run the generator to produce the C file.
4. Verify visually with the desktop viewer.
5. Ensure `data_size == 76*76*3` and descriptor fields match this guide.
6. Include the C file and `LV_IMG_DECLARE(...)` the symbol.

## Common pitfalls

- Wrong byte order: ensure `lo, hi, alpha` and `LV_COLOR_16_SWAP = 1`.
- Missing alpha: background not transparent → check source PNG alpha and generator output.
- Mismatched format: descriptor must be `LV_IMG_CF_TRUE_COLOR_ALPHA`.
- Size mismatch: header width/height must match the embedded data.

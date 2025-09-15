# LVGL Icon Generator

A simple GUI tool to generate complete LVGL icon C files with all color depth variants from a single input source.

## Features

- **Simple GUI Interface**: Uses tkinter (built into Python, no external dependencies)
- **Multiple Input Methods**: 
  - Load image files (PNG, JPG, BMP, etc.)
  - Paste raw pixel data in hex format
- **Complete Output**: Generates C files with conditional compilation for all LV_COLOR_DEPTH values
- **Format Support**: 1-bit, 8-bit, 16-bit, 24-bit, 32-bit color depths
- **LVGL Compatible**: Output matches the structure of existing LVGL icon files

## Usage

1. Run the tool:
   ```bash
   python3 generator.py
   ```

2. Configure the icon:
   - Set the icon name
   - Set width and height dimensions

3. Load input data:
   - **Load Image File**: Select a PNG/JPG/BMP image file
   - **Load Raw Pixel Data**: Paste hex pixel data (RGBA format)
   - Example raw data format: `0xff, 0x00, 0x00, 0xff, 0x80, 0x80, 0x80, 0xff`

4. Generate C file:
   - Click "Generate C File"
   - Choose output location
   - The tool creates a complete C file with all color depth variants

## Output Format

The generated C file includes:

```c
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif
#ifndef LV_ATTRIBUTE_IMG_ICON_NAME
#define LV_ATTRIBUTE_IMG_ICON_NAME
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_IMG_ICON_NAME uint8_t icon_name_map[] = {
#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
  /*Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit*/
  // ... 8-bit data ...
#endif

#if LV_COLOR_DEPTH == 16
  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/
  // ... 16-bit RGB565 + Alpha data ...
#endif

#if LV_COLOR_DEPTH == 24
  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit*/
  // ... 24-bit RGB data ...
#endif

#if LV_COLOR_DEPTH == 32
  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit, Alpha: 8 bit*/
  // ... 32-bit RGBA data ...
#endif
};

const lv_img_dsc_t icon_name = {
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 76,
  .header.h = 76,
  .data_size = 76 * 76 * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = icon_name_map,
};
```

## Requirements

- Python 3.6+
- tkinter (usually included with Python)
- PIL/Pillow (for image loading)

Install PIL if not available:
```bash
pip install Pillow
```

## Example

1. Create a 76x76 pixel microphone icon in your favorite image editor
2. Save as PNG with transparency
3. Load into the generator
4. Set icon name to "icon_recorder"
5. Generate C file
6. Use in your LVGL project with `LV_IMG_DECLARE(icon_recorder)` and `lv_img_set_src(img, &icon_recorder)`

### LVGL Image Viewer (tkinter)

Small Python tool to open and render LVGL C image files (lv_img_dsc_t) without extra dependencies.

Features:
- Select a `.c` file containing an `lv_img_dsc_t` and preview the image
- Parse `.header` fields: `cf`, `w`, `h`, `data_size`
- Toggle decoding format to simulate different LVGL color formats
- Compare view renders several formats side-by-side
- Options: LV_COLOR_DEPTH (16/32), LV_COLOR_16_SWAP (on/off), scale

Planned formats (initial support implemented incrementally):
- True color (RGB565, RGBA8888-as-RGB)
- True color with alpha (RGB565+A, RGBA8888)
- Alpha-only (1/2/4/8-bit)
- Indexed (1/2/4/8-bit palette): RGB565+A or RGBA8888 depending on LV_COLOR_DEPTH

Requirements:
- Python 3.8+ on Linux (uses built-in tkinter)
- No additional libraries will be installed without approval

Usage:
```bash
python3 viewer.py
```

Notes:
- The viewer attempts to auto-detect the format from `.header.cf`. You can override it in the UI to experiment.
- RGB565 byte/word order can be toggled via LV_COLOR_16_SWAP in the UI.
- Compare button opens a grid with multiple decoders to visualize differences.



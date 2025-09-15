# Generator Tool #
python3 /home/ab/Coding/ESP32-S3-BOX/VoiceRecorder/tools/lvgl_icon_generator/generator_simple.py

# Viewer Tool #

To view the LVGL graphics before flashing I built this viewer tool. 
To run use the command:
python3 /home/ab/Coding/ESP32-S3-BOX/VoiceRecorder/tools/lvgl_image_viewer/viewer.py


# LVGL Icon Format for ESP32-S3-BOX-3

- **Color format:** `LV_IMG_CF_TRUE_COLOR_ALPHA`  
- **Color depth:** `LV_COLOR_DEPTH = 16` (RGB565)  
- **Byte order:** `LV_COLOR_16_SWAP = 1`  

## Pixel Encoding
Each pixel = **3 bytes**  

- `color_lo` = low byte of RGB565  
- `color_hi` = high byte of RGB565  

## Colors
- **Foreground (icon shape):** dark red (same as `icon_network.c`), `alpha = 255`  
- **Background:** black `(0,0,0)`, `alpha = 0` (fully transparent)  

## LVGL Descriptor Example
```c
const lv_img_dsc_t icon_recorder = {
  .header.always_zero = 0,
  .header.w = 76,
  .header.h = 76,
  .data_size = sizeof(icon_recorder_map),
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .data = icon_recorder_map,
};

#!/usr/bin/env python3
"""
LVGL Icon Generator - Command Line Version

Simple command-line tool to generate LVGL icon C files from raw pixel data.
No GUI dependencies required.

Usage:
    python3 generator_cli.py --name icon_name --width 76 --height 76 --data "0xff,0x00,0x00,0xff,..."
    python3 generator_cli.py --name icon_name --width 76 --height 76 --file input.txt
    python3 generator_cli.py --example
"""

import argparse
import sys
import os

def parse_hex_data(data_str):
    """Parse hex data string into RGBA tuples"""
    # Clean up the input
    data_str = data_str.replace(',', ' ').replace('0x', '').replace(' ', ' ')
    hex_values = [h for h in data_str.split() if h]
    
    if len(hex_values) % 4 != 0:
        raise ValueError(f"Pixel data must be in RGBA format (4 bytes per pixel), got {len(hex_values)} values")
    
    pixel_data = []
    for i in range(0, len(hex_values), 4):
        r = int(hex_values[i], 16)
        g = int(hex_values[i+1], 16)
        b = int(hex_values[i+2], 16)
        a = int(hex_values[i+3], 16)
        pixel_data.append((r, g, b, a))
    
    return pixel_data

def create_example_data():
    """Create example red square data"""
    width, height = 32, 32
    data = []
    
    for y in range(height):
        for x in range(width):
            if x < 4 or x >= width-4 or y < 4 or y >= height-4:
                # Transparent border
                data.extend([0x00, 0x00, 0x00, 0x00])
            else:
                # Red square
                data.extend([0xff, 0x00, 0x00, 0xff])
    
    return data, width, height

def generate_c_file(icon_name, width, height, pixel_data, output_file):
    """Generate the complete C file"""
    
    # Ensure we have enough pixel data
    pixel_count = width * height
    if len(pixel_data) < pixel_count:
        # Pad with transparent pixels
        pixel_data.extend([(0, 0, 0, 0)] * (pixel_count - len(pixel_data)))
    else:
        # Truncate if too many
        pixel_data = pixel_data[:pixel_count]
    
    # Header
    content = f'''#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif
#ifndef LV_ATTRIBUTE_IMG_{icon_name.upper()}
#define LV_ATTRIBUTE_IMG_{icon_name.upper()}
#endif
const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_IMG_{icon_name.upper()} uint8_t {icon_name}_map[] = {{
'''
    
    # Generate data for different color depths
    content += generate_color_depth_data(pixel_data)
    
    # Footer
    content += f'''}};

const lv_img_dsc_t {icon_name} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {width},
  .header.h = {height},
  .data_size = {width} * {height} * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = {icon_name}_map,
}};
'''
    
    # Write file
    with open(output_file, 'w') as f:
        f.write(content)
    
    return content

def generate_color_depth_data(pixel_data):
    """Generate pixel data for different color depths"""
    content = ""
    
    # LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
    content += "#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8\n"
    content += "  /*Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit*/\n"
    
    # Generate 8-bit indexed data
    data_8bit = []
    for r, g, b, a in pixel_data:
        if a < 128:  # Transparent
            data_8bit.append(0x00)
        else:
            # Convert to 8-bit: A8R3G3B2
            r3 = (r >> 5) & 0x7
            g3 = (g >> 5) & 0x7
            b2 = (b >> 6) & 0x3
            a8 = 0xFF if a >= 128 else 0x00
            data_8bit.append(a8)
            
    content += format_byte_array(data_8bit)
    content += "\n#endif\n\n"
    
    # LV_COLOR_DEPTH == 16
    content += "#if LV_COLOR_DEPTH == 16\n"
    content += "  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/\n"
    
    # Generate 16-bit RGB565 + Alpha data
    data_16bit = []
    for r, g, b, a in pixel_data:
        if a < 8:  # Mostly transparent
            data_16bit.extend([0x00, 0x00, 0x00])  # RGB565 + Alpha
        else:
            # Convert to RGB565
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            
            # Little endian
            data_16bit.append(rgb565 & 0xFF)
            data_16bit.append((rgb565 >> 8) & 0xFF)
            data_16bit.append(a)
            
    content += format_byte_array(data_16bit)
    content += "\n#endif\n\n"
    
    # LV_COLOR_DEPTH == 24
    content += "#if LV_COLOR_DEPTH == 24\n"
    content += "  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit*/\n"
    
    # Generate 24-bit RGB data
    data_24bit = []
    for r, g, b, a in pixel_data:
        if a < 128:  # Transparent
            data_24bit.extend([0x00, 0x00, 0x00])
        else:
            data_24bit.extend([r, g, b])
            
    content += format_byte_array(data_24bit)
    content += "\n#endif\n\n"
    
    # LV_COLOR_DEPTH == 32
    content += "#if LV_COLOR_DEPTH == 32\n"
    content += "  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit, Alpha: 8 bit*/\n"
    
    # Generate 32-bit RGBA data
    data_32bit = []
    for r, g, b, a in pixel_data:
        data_32bit.extend([r, g, b, a])
        
    content += format_byte_array(data_32bit)
    content += "\n#endif\n"
    
    return content

def format_byte_array(data):
    """Format byte array as C code"""
    lines = []
    for i in range(0, len(data), 24):  # 24 bytes per line
        line = "  "
        for j in range(i, min(i + 24, len(data))):
            line += f"0x{data[j]:02x}"
            if j < min(i + 24, len(data)) - 1:
                line += ", "
        line += ","
        lines.append(line)
        
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description='Generate LVGL icon C files from pixel data')
    parser.add_argument('--name', help='Icon name (e.g., icon_microphone)')
    parser.add_argument('--width', type=int, default=76, help='Icon width (default: 76)')
    parser.add_argument('--height', type=int, default=76, help='Icon height (default: 76)')
    parser.add_argument('--data', help='Hex pixel data as comma-separated values (RGBA format)')
    parser.add_argument('--file', help='File containing hex pixel data')
    parser.add_argument('--output', help='Output C file (default: {name}.c)')
    parser.add_argument('--example', action='store_true', help='Generate example red square icon')
    
    args = parser.parse_args()
    
    try:
        # Handle example generation
        if args.example:
            data, width, height = create_example_data()
            icon_name = "icon_example"
            output_file = args.output or "icon_example.c"
            print(f"Generating example red square icon ({width}x{height})...")
        else:
            # Parse pixel data
            if args.data:
                data = parse_hex_data(args.data)
            elif args.file:
                with open(args.file, 'r') as f:
                    data_str = f.read().strip()
                data = parse_hex_data(data_str)
            else:
                print("Error: Must provide --data, --file, or --example")
                return 1
            
            if not args.name:
                print("Error: Must provide --name when not using --example")
                return 1
                
            icon_name = args.name
            width = args.width
            height = args.height
            output_file = args.output or f"{icon_name}.c"
        
        # Convert to RGBA tuples
        pixel_data = []
        for i in range(0, len(data), 4):
            if i + 3 < len(data):
                pixel_data.append((data[i], data[i+1], data[i+2], data[i+3]))
        
        # Generate C file
        content = generate_c_file(icon_name, width, height, pixel_data, output_file)
        
        print(f"Generated {output_file}")
        print(f"Icon: {icon_name} ({width}x{height})")
        print(f"File size: {len(content)} characters")
        print(f"Pixels: {len(pixel_data)}")
        
        # Show usage instructions
        print(f"\nUsage in your LVGL project:")
        print(f"1. Copy {output_file} to your project")
        print(f"2. Include the file:")
        print(f"   #include \"{output_file}\"")
        print(f"3. Declare the icon:")
        print(f"   LV_IMG_DECLARE({icon_name});")
        print(f"4. Use in your UI:")
        print(f"   lv_img_set_src(your_img_obj, &{icon_name});")
        
        return 0
        
    except Exception as e:
        print(f"Error: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())

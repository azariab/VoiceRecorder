#!/usr/bin/env python3
"""
LVGL Icon Generator Tool - Simple Version (No PIL dependency)

This is a simplified version that doesn't require PIL/Pillow.
It works with raw pixel data input only.

Usage:
    python3 generator_simple.py
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import struct
import os
import sys

class SimpleLVGLIconGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("LVGL Icon Generator - Simple Version")
        self.root.geometry("700x500")
        
        # Variables
        self.input_data = []
        self.icon_name = tk.StringVar(value="icon_example")
        self.image_width = tk.IntVar(value=76)
        self.image_height = tk.IntVar(value=76)
        
        self.setup_ui()
        
    def setup_ui(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        
        # Icon name
        ttk.Label(main_frame, text="Icon Name:").grid(row=0, column=0, sticky=tk.W, pady=5)
        ttk.Entry(main_frame, textvariable=self.icon_name, width=30).grid(row=0, column=1, sticky=(tk.W, tk.E), pady=5)
        
        # Image dimensions
        ttk.Label(main_frame, text="Width:").grid(row=1, column=0, sticky=tk.W, pady=5)
        ttk.Spinbox(main_frame, from_=1, to=512, textvariable=self.image_width, width=10).grid(row=1, column=1, sticky=tk.W, pady=5)
        
        ttk.Label(main_frame, text="Height:").grid(row=2, column=0, sticky=tk.W, pady=5)
        ttk.Spinbox(main_frame, from_=1, to=512, textvariable=self.image_height, width=10).grid(row=2, column=1, sticky=tk.W, pady=5)
        
        # Input section
        input_frame = ttk.LabelFrame(main_frame, text="Input Raw Pixel Data", padding="5")
        input_frame.grid(row=3, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        input_frame.columnconfigure(0, weight=1)
        
        # Instructions
        ttk.Label(input_frame, text="Paste hex pixel data:").grid(row=0, column=0, sticky=tk.W, pady=5)
        
        # Format selection
        format_frame = ttk.Frame(input_frame)
        format_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(format_frame, text="Input Format:").grid(row=0, column=0, sticky=tk.W, padx=(0, 10))
        self.input_format = tk.StringVar(value="RGBA_8888")
        
        # Create format options
        format_options = [
            ("RGBA 8-bit (R8G8B8A8)", "RGBA_8888"),
            ("RGB 8-bit (R8G8B8)", "RGB_888"),
            ("RGB565 + Alpha (R5G6B5A8)", "RGB565_ALPHA"),
            ("RGB565 Swapped + Alpha (B5G6R5A8)", "RGB565_SWAP_ALPHA"),
        ]
        
        for i, (label, value) in enumerate(format_options):
            ttk.Radiobutton(format_frame, text=label, variable=self.input_format, value=value).grid(row=0, column=i+1, sticky=tk.W, padx=(0, 10))
        
        # Raw data input
        self.raw_data_text = scrolledtext.ScrolledText(input_frame, height=8, width=60)
        self.raw_data_text.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=5)
        
        # Example data button
        ttk.Button(input_frame, text="Load Example Data", command=self.load_example_data).grid(row=3, column=0, pady=5)
        
        # Parse button
        ttk.Button(input_frame, text="Parse Pixel Data", command=self.parse_raw_data).grid(row=4, column=0, pady=5)
        
        # Status
        self.status_label = ttk.Label(main_frame, text="Ready - Enter pixel data in hex format (e.g., 0xff, 0x00, 0x00, 0xff)")
        self.status_label.grid(row=4, column=0, columnspan=2, pady=5)
        
        # Generate button
        ttk.Button(main_frame, text="Generate C File", command=self.generate_c_file).grid(row=5, column=0, columnspan=2, pady=20)
        
    def load_example_data(self):
        """Load example pixel data for a simple red square"""
        width = self.image_width.get()
        height = self.image_height.get()
        
        # Create a simple red square with transparent border
        example_data = []
        
        for y in range(height):
            for x in range(width):
                if x < 4 or x >= width-4 or y < 4 or y >= height-4:
                    # Transparent border
                    example_data.extend([0x00, 0x00, 0x00, 0x00])
                else:
                    # Red square
                    example_data.extend([0xff, 0x00, 0x00, 0xff])
        
        # Format as hex string
        hex_data = ", ".join([f"0x{b:02x}" for b in example_data])
        
        self.raw_data_text.delete(1.0, tk.END)
        self.raw_data_text.insert(1.0, hex_data)
        
        self.status_label.config(text=f"Loaded example red square data ({width}x{height})")
        
    def parse_raw_data(self):
        """Parse raw pixel data from text input"""
        try:
            text = self.raw_data_text.get(1.0, tk.END).strip()
            if not text:
                messagebox.showwarning("Warning", "No raw data entered")
                return
                
            # Parse hex data
            hex_data = text.replace(',', ' ').replace('0x', '').split()
            pixel_data = [int(h, 16) for h in hex_data if h]
            
            # Determine format and bytes per pixel
            input_format = self.input_format.get()
            bytes_per_pixel = self.get_bytes_per_pixel(input_format)
            
            if len(pixel_data) % bytes_per_pixel != 0:
                messagebox.showerror("Error", f"Pixel data must be in {input_format} format ({bytes_per_pixel} bytes per pixel)")
                return
                
            # Convert to RGBA tuples based on input format
            self.input_data = self.convert_to_rgba(pixel_data, input_format)
            
            # Check size
            width = self.image_width.get()
            height = self.image_height.get()
            expected_size = width * height
            
            if len(self.input_data) != expected_size:
                messagebox.showwarning("Warning", f"Expected {expected_size} pixels, got {len(self.input_data)}")
                
            self.status_label.config(text=f"Parsed {len(self.input_data)} pixels successfully ({input_format} format)")
                
        except Exception as e:
            messagebox.showerror("Error", f"Failed to parse raw data: {str(e)}")
            
    def get_bytes_per_pixel(self, format_type):
        """Get bytes per pixel for given format"""
        if format_type == "RGBA_8888":
            return 4
        elif format_type == "RGB_888":
            return 3
        elif format_type in ["RGB565_ALPHA", "RGB565_SWAP_ALPHA"]:
            return 3  # 2 bytes RGB565 + 1 byte alpha
        else:
            return 4
            
    def convert_to_rgba(self, pixel_data, format_type):
        """Convert pixel data to RGBA tuples based on input format"""
        rgba_data = []
        
        if format_type == "RGBA_8888":
            # R8G8B8A8
            for i in range(0, len(pixel_data), 4):
                rgba_data.append((pixel_data[i], pixel_data[i+1], pixel_data[i+2], pixel_data[i+3]))
                
        elif format_type == "RGB_888":
            # R8G8B8 (add full alpha)
            for i in range(0, len(pixel_data), 3):
                rgba_data.append((pixel_data[i], pixel_data[i+1], pixel_data[i+2], 0xFF))
                
        elif format_type == "RGB565_ALPHA":
            # R5G6B5A8 (little endian RGB565 + alpha)
            for i in range(0, len(pixel_data), 3):
                rgb565 = pixel_data[i] | (pixel_data[i+1] << 8)
                alpha = pixel_data[i+2]
                
                # Extract RGB565 components
                r = ((rgb565 >> 11) & 0x1F) << 3
                g = ((rgb565 >> 5) & 0x3F) << 2
                b = (rgb565 & 0x1F) << 3
                
                rgba_data.append((r, g, b, alpha))
                
        elif format_type == "RGB565_SWAP_ALPHA":
            # B5G6R5A8 (swapped RGB565 + alpha)
            for i in range(0, len(pixel_data), 3):
                rgb565 = pixel_data[i] | (pixel_data[i+1] << 8)
                alpha = pixel_data[i+2]
                
                # Extract swapped RGB565 components (BGR order)
                b = ((rgb565 >> 11) & 0x1F) << 3
                g = ((rgb565 >> 5) & 0x3F) << 2
                r = (rgb565 & 0x1F) << 3
                
                rgba_data.append((r, g, b, alpha))
                
        return rgba_data
            
    def generate_c_file(self):
        """Generate complete C file with all color depth variants"""
        if not self.input_data:
            messagebox.showerror("Error", "No input data loaded. Please parse pixel data first.")
            return
            
        try:
            # Get output file
            output_path = filedialog.asksaveasfilename(
                title="Save C File",
                defaultextension=".c",
                filetypes=[("C files", "*.c"), ("All files", "*.*")]
            )
            
            if not output_path:
                return
                
            # Generate C file content
            c_content = self.generate_c_content()
            
            # Write file
            with open(output_path, 'w') as f:
                f.write(c_content)
                
            self.status_label.config(text=f"Generated: {os.path.basename(output_path)}")
            messagebox.showinfo("Success", f"C file generated successfully!\nSaved to: {output_path}")
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to generate C file: {str(e)}")
            
    def generate_c_content(self):
        """Generate the complete C file content"""
        icon_name = self.icon_name.get()
        width = self.image_width.get()
        height = self.image_height.get()
        
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
        content += self.generate_color_depth_data()
        
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
        
        return content
        
    def generate_color_depth_data(self):
        """Generate pixel data for different color depths"""
        width = self.image_width.get()
        height = self.image_height.get()
        
        # Ensure we have enough pixel data
        pixel_count = width * height
        if len(self.input_data) < pixel_count:
            # Pad with transparent pixels
            self.input_data.extend([(0, 0, 0, 0)] * (pixel_count - len(self.input_data)))
        else:
            # Truncate if too many
            self.input_data = self.input_data[:pixel_count]
            
        content = ""
        
        # LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
        content += "#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8\n"
        content += "  /*Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit*/\n"
        
        # Generate 8-bit indexed data (A8R3G3B2 format)
        data_8bit = []
        for r, g, b, a in self.input_data:
            if a < 128:  # Transparent
                data_8bit.append(0x00)
            else:
                # Convert to 8-bit: A8R3G3B2
                r3 = (r >> 5) & 0x7
                g3 = (g >> 5) & 0x7
                b2 = (b >> 6) & 0x3
                a8 = 0xFF if a >= 128 else 0x00
                data_8bit.append(a8)
                
        content += self.format_byte_array(data_8bit)
        content += "\n#endif\n\n"
        
        # LV_COLOR_DEPTH == 16 (RGB565 + Alpha)
        content += "#if LV_COLOR_DEPTH == 16\n"
        content += "  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/\n"
        
        # Generate 16-bit RGB565 + Alpha data (little endian)
        data_16bit = []
        for r, g, b, a in self.input_data:
            if a < 8:  # Mostly transparent
                data_16bit.extend([0x00, 0x00, 0x00])  # RGB565 + Alpha
            else:
                # Convert to RGB565
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Little endian format
                data_16bit.append(rgb565 & 0xFF)
                data_16bit.append((rgb565 >> 8) & 0xFF)
                data_16bit.append(a)
                
        content += self.format_byte_array(data_16bit)
        content += "\n#endif\n\n"
        
        # LV_COLOR_DEPTH == 16 (RGB565 Swapped + Alpha) - Alternative format
        content += "#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 1\n"
        content += "  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit (swapped)*/\n"
        
        # Generate 16-bit BGR565 + Alpha data (swapped format)
        data_16bit_swap = []
        for r, g, b, a in self.input_data:
            if a < 8:  # Mostly transparent
                data_16bit_swap.extend([0x00, 0x00, 0x00])  # BGR565 + Alpha
            else:
                # Convert to BGR565 (swapped)
                b5 = (b >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                r5 = (r >> 3) & 0x1F
                bgr565 = (b5 << 11) | (g6 << 5) | r5
                
                # Little endian format
                data_16bit_swap.append(bgr565 & 0xFF)
                data_16bit_swap.append((bgr565 >> 8) & 0xFF)
                data_16bit_swap.append(a)
                
        content += self.format_byte_array(data_16bit_swap)
        content += "\n#endif\n\n"
        
        # LV_COLOR_DEPTH == 24
        content += "#if LV_COLOR_DEPTH == 24\n"
        content += "  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit*/\n"
        
        # Generate 24-bit RGB data
        data_24bit = []
        for r, g, b, a in self.input_data:
            if a < 128:  # Transparent
                data_24bit.extend([0x00, 0x00, 0x00])
            else:
                data_24bit.extend([r, g, b])
                
        content += self.format_byte_array(data_24bit)
        content += "\n#endif\n\n"
        
        # LV_COLOR_DEPTH == 32
        content += "#if LV_COLOR_DEPTH == 32\n"
        content += "  /*Pixel format: Red: 8 bit, Green: 8 bit, Blue: 8 bit, Alpha: 8 bit*/\n"
        
        # Generate 32-bit RGBA data
        data_32bit = []
        for r, g, b, a in self.input_data:
            data_32bit.extend([r, g, b, a])
            
        content += self.format_byte_array(data_32bit)
        content += "\n#endif\n"
        
        return content
        
    def format_byte_array(self, data):
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
    root = tk.Tk()
    app = SimpleLVGLIconGenerator(root)
    root.mainloop()

if __name__ == "__main__":
    main()

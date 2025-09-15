#!/usr/bin/env python3
"""
LVGL Icon Generator Tool

This tool creates complete LVGL icon C files with all color depth variants
from a single input pixel map. It generates conditional compilation directives
similar to icon_network.c for maximum compatibility.

Features:
- Simple GUI interface using tkinter (no external dependencies)
- Input: Raw pixel data or image file
- Output: Complete C file with all LV_COLOR_DEPTH variants
- Supports: 1-bit, 8-bit, 16-bit, 24-bit, 32-bit color depths
- Generates proper LVGL structure with conditional compilation

Usage:
    python3 generator.py
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import struct
import os
import sys
from PIL import Image, ImageTk
import io

class LVGLIconGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("LVGL Icon Generator")
        self.root.geometry("800x600")
        
        # Variables
        self.input_data = None
        self.icon_name = tk.StringVar(value="icon_example")
        self.image_width = tk.IntVar(value=76)
        self.image_height = tk.IntVar(value=76)
        self.preview_image = None
        
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
        input_frame = ttk.LabelFrame(main_frame, text="Input", padding="5")
        input_frame.grid(row=3, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        input_frame.columnconfigure(0, weight=1)
        
        # File input
        ttk.Button(input_frame, text="Load Image File", command=self.load_image_file).grid(row=0, column=0, pady=5)
        ttk.Button(input_frame, text="Load Raw Pixel Data", command=self.load_raw_data).grid(row=0, column=1, pady=5)
        
        # Raw data input
        ttk.Label(input_frame, text="Or paste raw pixel data (hex format):").grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=5)
        self.raw_data_text = scrolledtext.ScrolledText(input_frame, height=6, width=60)
        self.raw_data_text.grid(row=2, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        # Preview
        preview_frame = ttk.LabelFrame(main_frame, text="Preview", padding="5")
        preview_frame.grid(row=4, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        
        self.preview_label = ttk.Label(preview_frame, text="No preview available")
        self.preview_label.grid(row=0, column=0, pady=5)
        
        # Generate button
        ttk.Button(main_frame, text="Generate C File", command=self.generate_c_file).grid(row=5, column=0, columnspan=2, pady=20)
        
        # Status
        self.status_label = ttk.Label(main_frame, text="Ready")
        self.status_label.grid(row=6, column=0, columnspan=2, pady=5)
        
    def load_image_file(self):
        """Load image file and convert to pixel data"""
        file_path = filedialog.askopenfilename(
            title="Select Image File",
            filetypes=[
                ("Image files", "*.png *.jpg *.jpeg *.bmp *.gif"),
                ("PNG files", "*.png"),
                ("All files", "*.*")
            ]
        )
        
        if file_path:
            try:
                # Load and convert image
                img = Image.open(file_path).convert('RGBA')
                
                # Resize if needed
                if img.size != (self.image_width.get(), self.image_height.get()):
                    img = img.resize((self.image_width.get(), self.image_height.get()), Image.LANCZOS)
                
                # Convert to pixel data
                self.input_data = list(img.getdata())
                self.update_preview(img)
                self.status_label.config(text=f"Loaded image: {os.path.basename(file_path)}")
                
            except Exception as e:
                messagebox.showerror("Error", f"Failed to load image: {str(e)}")
                
    def load_raw_data(self):
        """Load raw pixel data from text input"""
        try:
            text = self.raw_data_text.get(1.0, tk.END).strip()
            if not text:
                messagebox.showwarning("Warning", "No raw data entered")
                return
                
            # Parse hex data
            hex_data = text.replace(',', ' ').replace('0x', '').split()
            pixel_data = [int(h, 16) for h in hex_data if h]
            
            if len(pixel_data) % 4 != 0:
                messagebox.showerror("Error", "Pixel data must be in RGBA format (4 bytes per pixel)")
                return
                
            # Convert to RGBA tuples
            self.input_data = []
            for i in range(0, len(pixel_data), 4):
                if i + 3 < len(pixel_data):
                    self.input_data.append((pixel_data[i], pixel_data[i+1], pixel_data[i+2], pixel_data[i+3]))
            
            # Create preview image
            if self.input_data:
                width = self.image_width.get()
                height = self.image_height.get()
                expected_size = width * height
                
                if len(self.input_data) != expected_size:
                    messagebox.showwarning("Warning", f"Expected {expected_size} pixels, got {len(self.input_data)}")
                    
                # Create PIL image for preview
                img = Image.new('RGBA', (width, height))
                img.putdata(self.input_data[:expected_size])
                self.update_preview(img)
                self.status_label.config(text=f"Loaded {len(self.input_data)} pixels")
                
        except Exception as e:
            messagebox.showerror("Error", f"Failed to parse raw data: {str(e)}")
            
    def update_preview(self, img):
        """Update preview image"""
        try:
            # Resize for preview
            preview_img = img.resize((100, 100), Image.LANCZOS)
            self.preview_image = ImageTk.PhotoImage(preview_img)
            self.preview_label.config(image=self.preview_image, text="")
        except Exception as e:
            self.preview_label.config(text=f"Preview error: {str(e)}")
            
    def generate_c_file(self):
        """Generate complete C file with all color depth variants"""
        if not self.input_data:
            messagebox.showerror("Error", "No input data loaded")
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
        
        # Generate 8-bit indexed data
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
        
        # LV_COLOR_DEPTH == 16
        content += "#if LV_COLOR_DEPTH == 16\n"
        content += "  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/\n"
        
        # Generate 16-bit RGB565 + Alpha data
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
                
                # Little endian
                data_16bit.append(rgb565 & 0xFF)
                data_16bit.append((rgb565 >> 8) & 0xFF)
                data_16bit.append(a)
                
        content += self.format_byte_array(data_16bit)
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
    app = LVGLIconGenerator(root)
    root.mainloop()

if __name__ == "__main__":
    main()

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
try:
    from PIL import Image, ImageTk
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

class SimpleLVGLIconGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("LVGL Icon Generator - Simple Version")
        self.root.geometry("1000x600")
        
        # Variables
        self.input_data = []
        self.icon_name = tk.StringVar(value="icon_example")
        self.image_width = tk.IntVar(value=76)
        self.image_height = tk.IntVar(value=76)
        self.last_path = os.getcwd()
        
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
        format_frame.columnconfigure(1, weight=1)
        
        ttk.Label(format_frame, text="Input Format:").grid(row=0, column=0, sticky=tk.W, padx=(0, 10))
        self.input_format = tk.StringVar(value="RGBA_8888")
        
        # Create format options in a grid layout
        format_options = [
            ("RGBA 8-bit (R8G8B8A8)", "RGBA_8888"),
            ("RGB 8-bit (R8G8B8)", "RGB_888"),
            ("RGB565 + Alpha (R5G6B5A8)", "RGB565_ALPHA"),
            ("RGB565 Swapped + Alpha (B5G6R5A8)", "RGB565_SWAP_ALPHA"),
        ]
        
        # Arrange in 2 rows, 2 columns
        for i, (label, value) in enumerate(format_options):
            row = i // 2
            col = (i % 2) + 1
            ttk.Radiobutton(format_frame, text=label, variable=self.input_format, value=value).grid(row=row, column=col, sticky=tk.W, padx=(0, 20), pady=2)
        
        # Raw data input
        self.raw_data_text = scrolledtext.ScrolledText(input_frame, height=8, width=80)
        self.raw_data_text.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=5)
        
        # Button frame
        button_frame = ttk.Frame(input_frame)
        button_frame.grid(row=3, column=0, sticky=(tk.W, tk.E), pady=5)
        
        # Example data button
        ttk.Button(button_frame, text="Load Example Data", command=self.load_example_data).grid(row=0, column=0, padx=(0, 10))
        
        # Clear button
        ttk.Button(button_frame, text="Clear Data", command=self.clear_data).grid(row=0, column=1, padx=(0, 10))
        
        # Parse button
        ttk.Button(button_frame, text="Parse Pixel Data", command=self.parse_raw_data).grid(row=0, column=2)
        
        # Status
        self.status_label = ttk.Label(main_frame, text="Ready - Enter pixel data in hex format (e.g., 0xff, 0x00, 0x00, 0xff)")
        self.status_label.grid(row=4, column=0, columnspan=2, pady=5)
        
        # Preview section
        preview_frame = ttk.LabelFrame(main_frame, text="Preview (16-bit RGB565 without alpha)", padding="5")
        preview_frame.grid(row=5, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        preview_frame.columnconfigure(1, weight=1)
        
        ttk.Label(preview_frame, text="Format:").grid(row=0, column=0, sticky=tk.W, padx=(0, 10))
        self.preview_format = tk.StringVar(value="RGB565")
        preview_format_frame = ttk.Frame(preview_frame)
        preview_format_frame.grid(row=0, column=1, sticky=tk.W)
        
        ttk.Radiobutton(preview_format_frame, text="RGB565", variable=self.preview_format, value="RGB565", command=self.update_preview).grid(row=0, column=0, padx=(0, 10))
        ttk.Radiobutton(preview_format_frame, text="RGB565 Swapped", variable=self.preview_format, value="RGB565_SWAP", command=self.update_preview).grid(row=0, column=1, padx=(0, 10))
        ttk.Radiobutton(preview_format_frame, text="24-bit RGB", variable=self.preview_format, value="RGB24", command=self.update_preview).grid(row=0, column=2)
        
        # Preview image
        self.preview_label = ttk.Label(preview_frame, text="No preview available")
        self.preview_label.grid(row=1, column=0, columnspan=2, pady=10)
        
        # Generate button
        ttk.Button(main_frame, text="Generate C File", command=self.generate_c_file).grid(row=6, column=0, columnspan=2, pady=20)
        
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
        
    def clear_data(self):
        """Clear the raw data text area"""
        self.raw_data_text.delete(1.0, tk.END)
        self.input_data = []
        self.status_label.config(text="Data cleared")
        
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
            
            # Update preview after successful parsing
            self.update_preview()
                
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
        
    def update_preview(self):
        """Update the preview image based on current data and format selection"""
        if not self.input_data:
            self.preview_label.config(text="No preview available")
            return
            
        if PIL_AVAILABLE:
            self.update_preview_pil()
        else:
            self.update_preview_text()
            
    def update_preview_pil(self):
        """Update preview using PIL (if available)"""
        try:
            width = self.image_width.get()
            height = self.image_height.get()
            
            # Create preview data based on selected format
            preview_format = self.preview_format.get()
            preview_data = self.generate_preview_data(preview_format)
            
            # Create PIL image
            img = Image.new('RGB', (width, height))
            img.putdata(preview_data)
            
            # Resize for preview (max 200x200)
            max_size = 200
            if width > max_size or height > max_size:
                scale = max_size / max(width, height)
                new_width = int(width * scale)
                new_height = int(height * scale)
                img = img.resize((new_width, new_height), Image.LANCZOS)
            
            # Convert to PhotoImage
            self.preview_image = ImageTk.PhotoImage(img)
            self.preview_label.config(image=self.preview_image, text="")
            
        except Exception as e:
            self.preview_label.config(text=f"Preview error: {str(e)}")
            
    def update_preview_text(self):
        """Update preview using text representation (no PIL required)"""
        try:
            width = self.image_width.get()
            height = self.image_height.get()
            
            # Create ASCII art representation
            preview_format = self.preview_format.get()
            preview_text = self.generate_preview_text(preview_format)
            
            # Update label with text preview
            self.preview_label.config(text=preview_text, font=("Courier", 8))
            
        except Exception as e:
            self.preview_label.config(text=f"Preview error: {str(e)}")
            
    def generate_preview_data(self, preview_format):
        """Generate preview pixel data in the selected format (without alpha)"""
        preview_data = []
        
        for r, g, b, a in self.input_data:
            if preview_format == "RGB565":
                # Convert to RGB565 (no alpha)
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Convert back to 24-bit for display
                r_out = ((rgb565 >> 11) & 0x1F) << 3
                g_out = ((rgb565 >> 5) & 0x3F) << 2
                b_out = (rgb565 & 0x1F) << 3
                
            elif preview_format == "RGB565_SWAP":
                # Convert to BGR565 (swapped, no alpha)
                b5 = (b >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                r5 = (r >> 3) & 0x1F
                bgr565 = (b5 << 11) | (g6 << 5) | r5
                
                # Convert back to 24-bit for display (swapped back)
                b_out = ((bgr565 >> 11) & 0x1F) << 3
                g_out = ((bgr565 >> 5) & 0x3F) << 2
                r_out = (bgr565 & 0x1F) << 3
                
            else:  # RGB24
                # Use original RGB values (ignore alpha)
                r_out = r
                g_out = g
                b_out = b
            
            # Apply alpha blending with black background for transparency
            if a < 128:  # Transparent
                preview_data.append((0, 0, 0))  # Black background
            else:
                # Blend with black background based on alpha
                alpha_factor = a / 255.0
                r_out = int(r_out * alpha_factor)
                g_out = int(g_out * alpha_factor)
                b_out = int(b_out * alpha_factor)
                preview_data.append((r_out, g_out, b_out))
        
        return preview_data
        
    def generate_preview_text(self, preview_format):
        """Generate ASCII art preview of the icon"""
        width = self.image_width.get()
        height = self.image_height.get()
        
        # Scale down for text preview (max 40x20)
        max_width = 40
        max_height = 20
        
        scale_x = max_width / width if width > max_width else 1
        scale_y = max_height / height if height > max_height else 1
        scale = min(scale_x, scale_y, 1)  # Don't scale up
        
        text_width = int(width * scale)
        text_height = int(height * scale)
        
        lines = []
        lines.append(f"Preview ({preview_format}): {width}x{height} -> {text_width}x{text_height}")
        lines.append("=" * (text_width + 2))
        
        # Sample pixels for text representation
        for y in range(text_height):
            line = "|"
            for x in range(text_width):
                # Map text coordinates back to original image
                orig_x = int(x / scale) if scale < 1 else x
                orig_y = int(y / scale) if scale < 1 else y
                
                if orig_x < width and orig_y < height:
                    pixel_idx = orig_y * width + orig_x
                    if pixel_idx < len(self.input_data):
                        r, g, b, a = self.input_data[pixel_idx]
                        
                        # Apply format conversion
                        if preview_format == "RGB565":
                            r5 = (r >> 3) & 0x1F
                            g6 = (g >> 2) & 0x3F
                            b5 = (b >> 3) & 0x1F
                            rgb565 = (r5 << 11) | (g6 << 5) | b5
                            r_out = ((rgb565 >> 11) & 0x1F) << 3
                            g_out = ((rgb565 >> 5) & 0x3F) << 2
                            b_out = (rgb565 & 0x1F) << 3
                        elif preview_format == "RGB565_SWAP":
                            b5 = (b >> 3) & 0x1F
                            g6 = (g >> 2) & 0x3F
                            r5 = (r >> 3) & 0x1F
                            bgr565 = (b5 << 11) | (g6 << 5) | r5
                            b_out = ((bgr565 >> 11) & 0x1F) << 3
                            g_out = ((bgr565 >> 5) & 0x3F) << 2
                            r_out = (bgr565 & 0x1F) << 3
                        else:  # RGB24
                            r_out, g_out, b_out = r, g, b
                        
                        # Choose character based on brightness and alpha
                        if a < 64:
                            char = " "  # Transparent
                        elif a < 128:
                            char = "."  # Semi-transparent
                        else:
                            brightness = (r_out + g_out + b_out) / 3
                            if brightness < 85:
                                char = "#"  # Dark
                            elif brightness < 170:
                                char = "+"  # Medium
                            else:
                                char = "*"  # Bright
                    else:
                        char = "?"
                else:
                    char = " "
                    
                line += char
            line += "|"
            lines.append(line)
        
        lines.append("=" * (text_width + 2))
        lines.append(f"Format: {preview_format}")
        lines.append("Legend: * bright, + medium, # dark, . semi-trans, (space) transparent")
        
        return "\n".join(lines)
            
    def generate_c_file(self):
        """Generate complete C file with all color depth variants"""
        if not self.input_data:
            messagebox.showerror("Error", "No input data loaded. Please parse pixel data first.")
            return
            
        try:
            # Get output file
            output_path = filedialog.asksaveasfilename(
                title="Save C File",
                initialdir=self.last_path,
                defaultextension=".c",
                filetypes=[("C files", "*.c"), ("All files", "*.*")]
            )
            
            if not output_path:
                return
            
            self.last_path = os.path.dirname(output_path)
                
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
        content += "#if LV_COLOR_DEPTH == 16\n#if LV_COLOR_16_SWAP == 0\n"
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
        content += "#else\n"
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
        content += "\n#endif\n#endif\n\n"
        
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

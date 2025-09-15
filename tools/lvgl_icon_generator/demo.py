#!/usr/bin/env python3
"""
Demo script showing how to use the LVGL Icon Generator programmatically
"""

import sys
import os
sys.path.append(os.path.dirname(__file__))

from PIL import Image, ImageDraw
from generator import LVGLIconGenerator
import tkinter as tk

def create_microphone_icon():
    """Create a simple microphone icon for demo"""
    # Create 76x76 image with transparency
    img = Image.new('RGBA', (76, 76), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Microphone body (vertical rectangle)
    body_x = 30
    body_y = 20
    body_w = 16
    body_h = 40
    draw.rectangle([body_x, body_y, body_x + body_w, body_y + body_h], 
                   fill=(200, 50, 50, 255), outline=(150, 30, 30, 255), width=2)
    
    # Microphone head (oval)
    head_x = 25
    head_y = 15
    head_w = 26
    head_h = 20
    draw.ellipse([head_x, head_y, head_x + head_w, head_y + head_h], 
                 fill=(200, 50, 50, 255), outline=(150, 30, 30, 255), width=2)
    
    # Microphone stand (horizontal line)
    stand_y = body_y + body_h + 5
    draw.line([body_x - 5, stand_y, body_x + body_w + 5, stand_y], 
              fill=(150, 30, 30, 255), width=3)
    
    # Microphone base (small rectangle)
    base_x = body_x + 2
    base_y = stand_y + 5
    base_w = 12
    base_h = 8
    draw.rectangle([base_x, base_y, base_x + base_w, base_y + base_h], 
                   fill=(150, 30, 30, 255))
    
    return img

def demo_programmatic_usage():
    """Demonstrate using the generator programmatically"""
    print("LVGL Icon Generator Demo")
    print("========================")
    
    # Create a test icon
    print("Creating microphone icon...")
    mic_img = create_microphone_icon()
    mic_img.save("demo_microphone.png")
    print("Saved demo_microphone.png")
    
    # Create generator instance (hidden window)
    root = tk.Tk()
    root.withdraw()  # Hide the window
    
    generator = LVGLIconGenerator(root)
    
    # Set up the generator with our data
    generator.input_data = list(mic_img.getdata())
    generator.icon_name.set("icon_microphone")
    generator.image_width.set(76)
    generator.image_height.set(76)
    
    # Generate C content
    print("Generating C file content...")
    c_content = generator.generate_c_content()
    
    # Save the generated C file
    output_file = "demo_microphone.c"
    with open(output_file, "w") as f:
        f.write(c_content)
    
    print(f"Generated {output_file}")
    print(f"File size: {len(c_content)} characters")
    
    # Show some statistics
    lines = c_content.count('\n')
    print(f"Lines of code: {lines}")
    
    # Check for different color depth sections
    color_depths = []
    if "LV_COLOR_DEPTH == 1" in c_content:
        color_depths.append("1/8-bit")
    if "LV_COLOR_DEPTH == 16" in c_content:
        color_depths.append("16-bit")
    if "LV_COLOR_DEPTH == 24" in c_content:
        color_depths.append("24-bit")
    if "LV_COLOR_DEPTH == 32" in c_content:
        color_depths.append("32-bit")
    
    print(f"Supported color depths: {', '.join(color_depths)}")
    
    # Show usage instructions
    print("\nUsage in your LVGL project:")
    print("1. Copy the generated C file to your project")
    print("2. Include the header:")
    print("   #include \"demo_microphone.c\"")
    print("3. Declare the icon:")
    print("   LV_IMG_DECLARE(icon_microphone);")
    print("4. Use in your UI:")
    print("   lv_img_set_src(your_img_obj, &icon_microphone);")
    
    root.destroy()
    print("\nDemo completed!")

if __name__ == "__main__":
    demo_programmatic_usage()

#!/usr/bin/env python3
"""
Test script for LVGL Icon Generator
Creates a simple test icon and generates C file
"""

import sys
import os
sys.path.append(os.path.dirname(__file__))

from PIL import Image
from generator import LVGLIconGenerator

def create_test_icon():
    """Create a simple test icon"""
    # Create a 32x32 test icon with a red circle
    img = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    
    # Draw a simple red circle
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img)
    draw.ellipse([4, 4, 28, 28], fill=(255, 0, 0, 255))
    
    return img

def test_generator():
    """Test the generator functionality"""
    print("Testing LVGL Icon Generator...")
    
    # Create test icon
    test_img = create_test_icon()
    print(f"Created test icon: {test_img.size} pixels")
    
    # Save test icon
    test_img.save("test_icon.png")
    print("Saved test_icon.png")
    
    # Test generator class
    import tkinter as tk
    
    # Create a minimal root window (hidden)
    root = tk.Tk()
    root.withdraw()  # Hide the window
    
    generator = LVGLIconGenerator(root)
    
    # Set test data
    generator.input_data = list(test_img.getdata())
    generator.icon_name.set("test_icon")
    generator.image_width.set(32)
    generator.image_height.set(32)
    
    # Generate C content
    c_content = generator.generate_c_content()
    
    # Save test output
    with open("test_output.c", "w") as f:
        f.write(c_content)
    
    print("Generated test_output.c")
    print(f"Content length: {len(c_content)} characters")
    
    # Check if content looks correct
    if "LV_COLOR_DEPTH == 1" in c_content and "LV_COLOR_DEPTH == 16" in c_content:
        print("✓ Conditional compilation directives found")
    else:
        print("✗ Missing conditional compilation directives")
    
    if "test_icon_map[]" in c_content:
        print("✓ Icon map array found")
    else:
        print("✗ Missing icon map array")
    
    if "lv_img_dsc_t test_icon" in c_content:
        print("✓ LVGL descriptor found")
    else:
        print("✗ Missing LVGL descriptor")
    
    root.destroy()
    print("Test completed!")

if __name__ == "__main__":
    test_generator()

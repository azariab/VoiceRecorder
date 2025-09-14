#!/usr/bin/env python3

import os
import re
import json
import struct
import tkinter as tk
from tkinter import filedialog, messagebox


# Map of LVGL image color formats to their integer values.
# Note: These values are consistent across LVGL v8 and v9.
LV_IMG_CF_MAP = {
    'LV_IMG_CF_TRUE_COLOR': 2,
    'LV_IMG_CF_TRUE_COLOR_ALPHA': 3,
    'LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED': 1,  # treat as true color with chroma key transparency
    'LV_IMG_CF_INDEXED_1BIT': 4,
    'LV_IMG_CF_INDEXED_2BIT': 5,
    'LV_IMG_CF_INDEXED_4BIT': 6,
    'LV_IMG_CF_INDEXED_8BIT': 7,
    'LV_IMG_CF_ALPHA_1BIT': 8,
    'LV_IMG_CF_ALPHA_2BIT': 9,
    'LV_IMG_CF_ALPHA_4BIT': 10,
    'LV_IMG_CF_ALPHA_8BIT': 11,
}


class LvglImage:
    """Represents a decoded LVGL image header and data."""
    def __init__(self):
        self.cf_name = None
        self.cf = None
        self.width = None
        self.height = None
        self.data_size = None
        self.data_bytes = b''


def parse_lvgl_c_file(path):
    """Parses an LVGL C header file to extract image data and properties."""
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    img = LvglImage()

    # Extract header fields using tolerant regexes
    cf_match = re.search(r'\.header\.cf\s*=\s*([A-Z0-9_]+)', text)
    w_match = re.search(r'\.header\.(w|width)\s*=\s*(\d+)', text)
    h_match = re.search(r'\.header\.(h|height)\s*=\s*(\d+)', text)
    ds_match = re.search(r'\.data_size\s*=\s*([^,;\n]+)', text)

    if cf_match:
        img.cf_name = cf_match.group(1)
        img.cf = LV_IMG_CF_MAP.get(img.cf_name)

    if w_match:
        img.width = int(w_match.group(2))
    if h_match:
        img.height = int(h_match.group(2))
    if ds_match:
        ds_expr = ds_match.group(1).strip()
        if re.fullmatch(r'\d+', ds_expr):
            img.data_size = int(ds_expr)
        else:
            img.data_size = None

    data_decl = re.search(r'const\s+[^;\n]*?\b(uint8_t|unsigned\s+char)\b\s+([a-zA-Z0-9_]+)\s*\[\s*\]\s*=\s*\{', text)
    data_bytes = b''
    if data_decl:
        name = data_decl.group(2)
        arr_re = re.compile(r'%s\s*\[\s*\]\s*=\s*\{([\s\S]*?)\};' % re.escape(name))
        m = arr_re.search(text)
        if m:
            body = m.group(1)
            values = re.findall(r'0x([0-9a-fA-F]{1,2})|\b(\d{1,3})\b', body)
            buf = bytearray()
            for hx, dec in values:
                if hx:
                    buf.append(int(hx, 16))
                elif dec:
                    iv = int(dec)
                    if 0 <= iv <= 255:
                        buf.append(iv)
            data_bytes = bytes(buf)

    img.data_bytes = data_bytes
    return img


def decode_true_color_rgb565(img, swap16=False):
    """Decodes a True Color (RGB565) image. LV_COLOR_DEPTH=16."""
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 2
    data = img.data_bytes
    if len(data) < expected:
        return None
    out = bytearray(w * h * 4)
    di = 0
    oi = 0
    for _ in range(w * h):
        lo = data[di]
        hi = data[di + 1]
        di += 2
        if swap16:
            lo, hi = hi, lo
        value = (hi << 8) | lo
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        r = (r5 * 255) // 31
        g = (g6 * 255) // 63
        b = (b5 * 255) // 31
        out[oi:oi + 4] = bytes((r, g, b, 255))
        oi += 4
    return bytes(out)


def decode_true_color_chroma_keyed_rgb565(img, chroma_key_value, swap16=False):
    """
    Decodes a True Color (RGB565) image with a chroma key.
    The chroma_key_value is provided as a 16-bit integer.
    """
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 2
    data = img.data_bytes
    if len(data) < expected:
        return None
    out = bytearray(w * h * 4)
    di = 0
    oi = 0
    
    for _ in range(w * h):
        lo = data[di]
        hi = data[di + 1]
        di += 2
        if swap16:
            lo, hi = hi, lo
        value = (hi << 8) | lo
        
        if value == chroma_key_value:
            r, g, b, a = 0, 0, 0, 0  # Transparent
        else:
            r5 = (value >> 11) & 0x1F
            g6 = (value >> 5) & 0x3F
            b5 = value & 0x1F
            r = (r5 * 255) // 31
            g = (g6 * 255) // 63
            b = (b5 * 255) // 31
            a = 255 # Opaque

        out[oi:oi + 4] = bytes((r, g, b, a))
        oi += 4
    return bytes(out)


def decode_true_color_alpha_v7_bgra(img):
    """Decodes a LVGL v7 True Color with Alpha (BGRA) image."""
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 4
    data = img.data_bytes
    if len(data) < expected:
        return None
    out = bytearray(expected)
    for i in range(0, expected, 4):
        b, g, r, a = data[i:i+4]
        out[i:i+4] = bytes((r, g, b, a))
    return bytes(out)


def decode_true_color_alpha_v8_rgba(img):
    """Decodes a LVGL v8/v9 True Color with Alpha (RGBA) image."""
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 4
    data = img.data_bytes
    if len(data) < expected:
        return None
    # For v8/v9, the data is already in the expected RGBA8888 format.
    return data[:expected]


def decode_true_color_rgba8888(img):
    """Decodes a 32-bit True Color (RGBA8888) image. Assumes BGRA byte order."""
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 4
    data = img.data_bytes
    if len(data) < expected:
        return None
    out = bytearray(expected)
    for i in range(0, expected, 4):
        b, g, r, _a = data[i:i+4]
        out[i:i+4] = bytes((r, g, b, 255))
    return bytes(out)


def decode_alpha_to_grayscale(img, bits):
    """
    Decodes an alpha-only image format by treating the data as a continuous
    bitstream. This correctly handles the pixel order.
    The output is a black shape with the correct transparency.
    """
    w, h = img.width, img.height
    if not (w and h):
        return None

    data = img.data_bytes
    expected_bits = w * h * bits
    expected_bytes = (expected_bits + 7) // 8

    # Use the data_size from the C file header if it's more accurate
    if img.data_size and img.data_size < expected_bytes:
        return None

    out = bytearray(w * h * 4)
    bit_offset = 0
    oi = 0
    mask = (1 << bits) - 1

    for _ in range(w * h):
        byte_index = bit_offset // 8
        bit_in_byte = bit_offset % 8
        
        # This shift calculation is for MSB-first packing, which is typical for LVGL.
        shift = 8 - bits - bit_in_byte

        try:
            byte = data[byte_index]
        except IndexError:
            # Reached end of data prematurely, break out
            break

        idx = (byte >> shift) & mask

        if bits == 1:
            alpha = 255 if idx else 0
        elif bits == 2:
            alpha = (idx * 85)
        elif bits == 4:
            alpha = (idx * 17)
        else:  # 8
            alpha = idx

        # Set R, G, B to black and set the alpha channel to the decoded alpha value
        out[oi:oi+4] = bytes((0, 0, 0, alpha))
        oi += 4
        bit_offset += bits

    return bytes(out)


def decode_indexed(img, bits, depth, swap16=False):
    """
    Decodes an indexed color image format by treating the data as a continuous
    bitstream, accounting for per-line padding.
    """
    w, h = img.width, img.height
    if not (w and h):
        return None
    colors = 1 << bits
    data = img.data_bytes
    if depth == '32':
        pal_stride = 4
    else: # 16
        pal_stride = 3
    pal_bytes = colors * pal_stride
    if len(data) < pal_bytes:
        return None
    palette_raw = data[:pal_bytes]
    pixels_raw = data[pal_bytes:]

    needed_idx_bits = w * h * bits
    needed_idx_bytes = (needed_idx_bits + 7) // 8
    if len(pixels_raw) < needed_idx_bytes:
        return None

    palette = []
    if pal_stride == 4:
        for i in range(0, pal_bytes, 4):
            b, g, r, a = palette_raw[i:i+4]
            palette.append((r, g, b, a))
    else:
        for i in range(0, pal_bytes, 3):
            lo = palette_raw[i]
            hi = palette_raw[i+1]
            a = palette_raw[i+2]
            if swap16:
                lo, hi = hi, lo
            value = (hi << 8) | lo
            r5 = (value >> 11) & 0x1F
            g6 = (value >> 5) & 0x3F
            b5 = value & 0x1F
            r = (r5 * 255) // 31
            g = (g6 * 255) // 63
            b = (b5 * 255) // 31
            palette.append((r, g, b, a))

    out = bytearray(w * h * 4)
    oi = 0
    
    # Calculate bytes per line, accounting for padding
    bytes_per_line = (w * bits + 7) // 8
    
    for y in range(h):
        line_start_byte_index = y * bytes_per_line
        bit_offset_in_line = 0
        
        for x in range(w):
            byte_index = line_start_byte_index + (bit_offset_in_line // 8)
            bit_in_byte = bit_offset_in_line % 8
            
            shift = 8 - bits - bit_in_byte
            
            try:
                byte = pixels_raw[byte_index]
            except IndexError:
                # Reached end of data
                break

            idx = (byte >> shift) & mask
            
            try:
                r, g, b, a = palette[idx]
                out[oi:oi+4] = bytes((r, g, b, a))
                oi += 4
                bit_offset_in_line += bits
            except IndexError:
                # Handle invalid palette index
                pass
    return bytes(out)


def rgba_bytes_to_photo(master, w, h, rgba_bytes):
    """Converts raw RGBA bytes into a Tkinter PhotoImage with a checkerboard background."""
    rgb = bytearray(w * h * 3)
    CHECKER_SIZE = 8
    C1 = (64, 64, 64)
    C2 = (96, 96, 96)

    for y in range(h):
        for x in range(w):
            if (x // CHECKER_SIZE) % 2 == (y // CHECKER_SIZE) % 2:
                bg_r, bg_g, bg_b = C1
            else:
                bg_r, bg_g, bg_b = C2

            i = (y * w + x) * 4
            r, g, b, a = rgba_bytes[i:i+4]

            alpha_f = a / 255.0
            inv_alpha_f = 1.0 - alpha_f
            out_r = int(r * alpha_f + bg_r * inv_alpha_f)
            out_g = int(g * alpha_f + bg_g * inv_alpha_f)
            out_b = int(b * alpha_f + bg_b * inv_alpha_f)

            ri = (y * w + x) * 3
            rgb[ri:ri+3] = bytes((out_r, out_g, out_b))

    header = f"P6\n{w} {h}\n255\n".encode('ascii')
    ppm = header + bytes(rgb)
    return tk.PhotoImage(data=ppm, format='PPM')


class ViewerApp:
    """The main application for the LVGL Image Viewer."""
    def __init__(self, master):
        self.master = master
        master.title('LVGL Image Viewer')
        self.state_path = os.path.join(os.path.dirname(__file__), '.viewer_state.json')
        self.last_dir = os.getcwd()
        self.lvgl_version = 'v8/v9'  # Default value
        self.depth = '16'
        self.swap = False
        self.chroma_key = '0xF81F' # Default chroma key is magenta
        self._load_state()

        ctrl = tk.Frame(master)
        ctrl.pack(side=tk.TOP, fill=tk.X, padx=8, pady=8)

        row1 = tk.Frame(ctrl)
        row1.pack(side=tk.TOP, fill=tk.X)
        self.path_var = tk.StringVar()
        tk.Entry(row1, textvariable=self.path_var).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))
        tk.Button(row1, text='Open .c', command=self.on_open).pack(side=tk.LEFT, padx=(0, 6))
        tk.Button(row1, text='Reload', command=self.on_reload).pack(side=tk.LEFT)

        row2 = tk.Frame(ctrl)
        row2.pack(side=tk.TOP, fill=tk.X, pady=(6, 0))

        self.cf_override = tk.StringVar(value='AUTO')
        tk.Label(row2, text='Format').pack(side=tk.LEFT, padx=(0, 4))
        fmt_menu = tk.OptionMenu(row2, self.cf_override, 'AUTO', *LV_IMG_CF_MAP.keys())
        fmt_menu.config(width=24)
        fmt_menu.pack(side=tk.LEFT)
        
        # New input for chroma key value
        tk.Label(row2, text='Chroma Key (hex)').pack(side=tk.LEFT, padx=(12, 4))
        self.chroma_key_var = tk.StringVar(value=self.chroma_key)
        tk.Entry(row2, textvariable=self.chroma_key_var, width=8).pack(side=tk.LEFT)


        self.lvgl_version_var = tk.StringVar(value=self.lvgl_version)
        tk.Label(row2, text='LVGL ver').pack(side=tk.LEFT, padx=(12, 4))
        ver_menu = tk.OptionMenu(row2, self.lvgl_version_var, 'v7', 'v8/v9')
        ver_menu.config(width=6)
        ver_menu.pack(side=tk.LEFT)

        self.depth_var = tk.StringVar(value=self.depth)
        tk.Label(row2, text='LV_COLOR_DEPTH').pack(side=tk.LEFT, padx=(12, 4))
        depth_menu = tk.OptionMenu(row2, self.depth_var, '16', '32')
        depth_menu.config(width=4)
        depth_menu.pack(side=tk.LEFT)

        self.swap_var = tk.BooleanVar(value=self.swap)
        tk.Checkbutton(row2, text='LV_COLOR_16_SWAP', variable=self.swap_var).pack(side=tk.LEFT, padx=(12, 0))

        actions = tk.Frame(row2)
        actions.pack(side=tk.RIGHT)
        tk.Button(actions, text='Render', command=self.on_render).pack(side=tk.LEFT, padx=(0, 6))
        tk.Button(actions, text='Compare', command=self.on_compare).pack(side=tk.LEFT)

        info = tk.Frame(master)
        info.pack(side=tk.TOP, fill=tk.X, padx=8, pady=(0, 8))
        self.info_var = tk.StringVar(value='No file loaded')
        self.info_label = tk.Label(info, textvariable=self.info_var, anchor='w', justify='left', wraplength=600)
        self.info_label.pack(fill=tk.X)

        self.canvas = tk.Canvas(master, width=300, height=200, bg='#202020', highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        self.current_img = None
        self.current_rgba = None
        self.loaded = None
        self.master.bind('<Configure>', self._on_resize)

    def on_open(self):
        initial_dir = self.last_dir if os.path.isdir(self.last_dir) else os.getcwd()
        path = filedialog.askopenfilename(
            title='Open LVGL C file',
            initialdir=initial_dir,
            filetypes=[('C files', '*.c'), ('All files', '*.*')]
        )
        if not path:
            return
        self.path_var.set(path)
        try:
            img = parse_lvgl_c_file(path)
            self.loaded = img
            self.info_var.set(self.format_info(img))
            self.last_dir = os.path.dirname(path) or self.last_dir
            self._save_state()
            self.on_render() # Auto-render on open
        except Exception as e:
            messagebox.showerror('Error', f'Failed to parse file:\n{e}')

    def on_render(self):
        if not self.loaded:
            messagebox.showinfo('Info', 'Open a C file first')
            return

        img = self.loaded
        cf_name = self.cf_override.get()
        if cf_name == 'AUTO':
            cf_name = img.cf_name

        depth = self.depth_var.get()
        swap = bool(self.swap_var.get())
        lvgl_version = self.lvgl_version_var.get()

        try:
            chroma_key_val = int(self.chroma_key_var.get(), 16)
        except (ValueError, IndexError):
            chroma_key_val = 0xF81F # Default to magenta if parsing fails
            messagebox.showwarning('Invalid Chroma Key', 'Invalid chroma key hex value. Defaulting to 0xF81F (magenta).')

        self.chroma_key = self.chroma_key_var.get()
        self._save_state()

        rgba = None
        try:
            if cf_name == 'LV_IMG_CF_TRUE_COLOR_ALPHA':
                if lvgl_version == 'v7':
                    rgba = decode_true_color_alpha_v7_bgra(img)
                else:  # v8/v9
                    rgba = decode_true_color_alpha_v8_rgba(img)
            elif cf_name == 'LV_IMG_CF_TRUE_COLOR':
                if depth == '16':
                    rgba = decode_true_color_rgb565(img, swap16=swap)
                else:
                    rgba = decode_true_color_rgba8888(img)
            elif cf_name == 'LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED':
                if depth == '16':
                    rgba = decode_true_color_chroma_keyed_rgb565(img, chroma_key_val, swap16=swap)
                else:
                    rgba = decode_true_color_rgba8888(img)
            elif cf_name in ('LV_IMG_CF_ALPHA_1BIT', 'LV_IMG_CF_ALPHA_2BIT', 'LV_IMG_CF_ALPHA_4BIT', 'LV_IMG_CF_ALPHA_8BIT'):
                bits = int(re.search(r'(\d+)BIT', cf_name).group(1))
                rgba = decode_alpha_to_grayscale(img, bits)
            elif cf_name == 'LV_IMG_CF_INDEXED_1BIT':
                rgba = decode_indexed(img, 1, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_2BIT':
                rgba = decode_indexed(img, 2, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_4BIT':
                rgba = decode_indexed(img, 4, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_8BIT':
                rgba = decode_indexed(img, 8, depth, swap16=swap)
            else:
                rgba = None
        except Exception as e:
            messagebox.showerror('Error', f'Decode failed:\n{e}')
            return

        if rgba is None:
            messagebox.showwarning('Warning', 'Unsupported format or decode failed (showing nothing).')
            self.current_img = None
            self.current_rgba = None
            self.canvas.delete('all')
            return

        # Check for all-transparent image and warn the user
        if all(rgba[i+3] == 0 for i in range(0, len(rgba), 4)):
             messagebox.showinfo('Possible issue detected', 'The decoded image is completely transparent. This often means the wrong color format was selected, or the original image was encoded with a transparent alpha channel.')


        self.current_rgba = rgba
        ph = rgba_bytes_to_photo(self.master, img.width, img.height, rgba)
        self.current_img = ph
        self.canvas.delete('all')
        self.canvas.config(width=max(320, img.width), height=max(240, img.height))
        self.canvas.create_image(0, 0, anchor='nw', image=ph)

    def format_info(self, img):
        return (
            f"cf: {img.cf_name or 'unknown'} | w: {img.width} | h: {img.height} | data_size: {img.data_size}\n"
            f"data bytes parsed: {len(img.data_bytes)}"
        )

    def on_compare(self):
        if not self.loaded:
            messagebox.showinfo('Info', 'Open a C file first')
            return
        img = self.loaded
        depth = self.depth_var.get()
        swap = bool(self.swap_var.get())
        lvgl_version = self.lvgl_version_var.get()
        try:
            chroma_key_val = int(self.chroma_key_var.get(), 16)
        except (ValueError, IndexError):
            chroma_key_val = 0xF81F # Default to magenta if parsing fails

        candidates = [
            'LV_IMG_CF_TRUE_COLOR_ALPHA',
            'LV_IMG_CF_TRUE_COLOR',
            'LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED',
            'LV_IMG_CF_ALPHA_1BIT',
            'LV_IMG_CF_ALPHA_2BIT',
            'LV_IMG_CF_ALPHA_4BIT',
            'LV_IMG_CF_ALPHA_8BIT',
            'LV_IMG_CF_INDEXED_1BIT',
            'LV_IMG_CF_INDEXED_2BIT',
            'LV_IMG_CF_INDEXED_4BIT',
            'LV_IMG_CF_INDEXED_8BIT',
        ]

        win = tk.Toplevel(self.master)
        win.title('Compare Formats')
        grid = tk.Frame(win, bg="#202020")
        grid.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        photos = []
        row = 0
        col = 0
        for name in candidates:
            try:
                if name == 'LV_IMG_CF_TRUE_COLOR_ALPHA':
                    if lvgl_version == 'v7':
                        rgba = decode_true_color_alpha_v7_bgra(img)
                    else:  # v8/v9
                        rgba = decode_true_color_alpha_v8_rgba(img)
                elif name == 'LV_IMG_CF_TRUE_COLOR':
                    rgba = decode_true_color_rgb565(img, swap16=swap) if depth == '16' else decode_true_color_rgba8888(img)
                elif name == 'LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED':
                    rgba = decode_true_color_chroma_keyed_rgb565(img, chroma_key_val, swap16=swap) if depth == '16' else decode_true_color_rgba8888(img)
                elif name in ('LV_IMG_CF_ALPHA_1BIT', 'LV_IMG_CF_ALPHA_2BIT', 'LV_IMG_CF_ALPHA_4BIT', 'LV_IMG_CF_ALPHA_8BIT'):
                    bits = int(re.search(r'(\d+)BIT', name).group(1))
                    rgba = decode_alpha_to_grayscale(img, bits)
                elif name == 'LV_IMG_CF_INDEXED_1BIT':
                    rgba = decode_indexed(img, 1, depth, swap16=swap)
                elif name == 'LV_IMG_CF_INDEXED_2BIT':
                    rgba = decode_indexed(img, 2, depth, swap16=swap)
                elif name == 'LV_IMG_CF_INDEXED_4BIT':
                    rgba = decode_indexed(img, 4, depth, swap16=swap)
                elif name == 'LV_IMG_CF_INDEXED_8BIT':
                    rgba = decode_indexed(img, 8, depth, swap16=swap)
                else:
                    rgba = None
            except Exception:
                rgba = None

            frame = tk.Frame(grid, bd=1, relief=tk.SOLID)
            frame.grid(row=row, column=col, padx=6, pady=6, sticky='nsew')
            tk.Label(frame, text=name, anchor='w', fg="white", bg="#202020").pack(fill=tk.X)
            canvas = tk.Canvas(frame, width=max(160, img.width), height=max(120, img.height), bg='#202020', highlightthickness=0)
            canvas.pack()

            if rgba is not None:
                ph = rgba_bytes_to_photo(win, img.width, img.height, rgba)
                photos.append(ph)
                canvas.create_image(0, 0, anchor='nw', image=ph)
            else:
                canvas.create_text(10, 10, anchor='nw', fill='white', text='N/A')

            col += 1
            if col >= 3:
                col = 0
                row += 1
        win._photos = photos

    def _on_resize(self, event):
        try:
            self.info_label.config(wraplength=max(300, event.width - 32))
        except Exception:
            pass

    def _load_state(self):
        try:
            with open(self.state_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            if isinstance(data, dict):
                if 'last_dir' in data:
                    self.last_dir = data['last_dir']
                if 'lvgl_version' in data and data['lvgl_version'] in ('v7', 'v8/v9'):
                    self.lvgl_version = data['lvgl_version']
                if 'depth' in data:
                    self.depth = data['depth']
                if 'swap' in data:
                    self.swap = data['swap']
                if 'chroma_key' in data:
                    self.chroma_key = data['chroma_key']
        except Exception:
            pass

    def _save_state(self):
        try:
            data = {'last_dir': self.last_dir, 'lvgl_version': self.lvgl_version_var.get(), 'depth': self.depth_var.get(), 'swap': self.swap_var.get(), 'chroma_key': self.chroma_key_var.get()}
            with open(self.state_path, 'w', encoding='utf-8') as f:
                json.dump(data, f)
        except Exception:
            pass

    def on_reload(self):
        path = (self.path_var.get() or '').strip()
        if not path:
            messagebox.showinfo('Info', 'No file path to reload')
            return
        if not os.path.isfile(path):
            messagebox.showerror('Error', f'File not found:\n{path}')
            return
        try:
            img = parse_lvgl_c_file(path)
            self.loaded = img
            self.info_var.set(self.format_info(img))
            self.last_dir = os.path.dirname(path) or self.last_dir
            self._save_state()
            self.on_render()
        except Exception as e:
            messagebox.showerror('Error', f'Failed to reload file:\n{e}')


def main():
    root = tk.Tk()
    app = ViewerApp(root)
    root.mainloop()


if __name__ == '__main__':
    main()

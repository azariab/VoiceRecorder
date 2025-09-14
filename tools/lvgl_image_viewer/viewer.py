#!/usr/bin/env python3

import os
import re
import json
import struct
import tkinter as tk
from tkinter import filedialog, messagebox


LV_IMG_CF_MAP = {
    'LV_IMG_CF_TRUE_COLOR': 2,
    'LV_IMG_CF_TRUE_COLOR_ALPHA': 3,
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
    def __init__(self):
        self.cf_name = None
        self.cf = None
        self.width = None
        self.height = None
        self.data_size = None
        self.data_bytes = b''


def parse_lvgl_c_file(path):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    img = LvglImage()

    # Extract header fields using tolerant regexes
    cf_match = re.search(r'\.header\.cf\s*=\s*([A-Z0-9_]+)', text)
    w_match = re.search(r'\.header\.(w|width)\s*=\s*(\d+)', text)
    h_match = re.search(r'\.header\.(h|height)\s*=\s*(\d+)', text)
    ds_match = re.search(r'\.data_size\s*=\s*(\d+)', text)

    if cf_match:
        img.cf_name = cf_match.group(1)
        img.cf = LV_IMG_CF_MAP.get(img.cf_name)

    if w_match:
        img.width = int(w_match.group(2))
    if h_match:
        img.height = int(h_match.group(2))
    if ds_match:
        img.data_size = int(ds_match.group(1))

    # Find the data array that the descriptor references.
    # Common pattern: const uint8_t <name>_map[] = { ... };
    data_decl = re.search(r'const\s+[^;\n]*?\b(uint8_t|unsigned\s+char)\b\s+([a-zA-Z0-9_]+)\s*\[\s*\]\s*=\s*\{', text)
    data_bytes = b''
    if data_decl:
        name = data_decl.group(2)
        arr_re = re.compile(r'%s\s*\[\s*\]\s*=\s*\{([\s\S]*?)\};' % re.escape(name))
        m = arr_re.search(text)
        if m:
            body = m.group(1)
            # Extract hex bytes like 0x12 or decimal bytes
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


def decode_true_color_alpha_rgb565a(img, swap16=False):
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 3
    data = img.data_bytes
    if len(data) < expected:
        # try to use data_size if provided
        if img.data_size and img.data_size <= len(data):
            data = data[:img.data_size]
        else:
            return None

    # Convert to RGBA8888 bytes for tkinter PhotoImage
    out = bytearray(w * h * 4)
    di = 0
    oi = 0
    for _ in range(w * h):
        lo = data[di]
        hi = data[di + 1]
        a = data[di + 2]
        di += 3
        if swap16:
            lo, hi = hi, lo
        value = (hi << 8) | lo
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        r = (r5 * 255) // 31
        g = (g6 * 255) // 63
        b = (b5 * 255) // 31
        out[oi:oi + 4] = bytes((r, g, b, a))
        oi += 4
    return bytes(out)


def decode_true_color_rgb565(img, swap16=False):
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


def decode_true_color_alpha_rgba8888(img):
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 4
    data = img.data_bytes
    if len(data) < expected:
        return None
    # Assume source is RGBA8888 already
    return bytes(data[:expected])


def decode_true_color_rgba8888(img):
    w, h = img.width, img.height
    if not (w and h):
        return None
    expected = w * h * 4
    data = img.data_bytes
    if len(data) < expected:
        return None
    # Assume source is RGBA8888; force alpha to 255
    out = bytearray(expected)
    di = 0
    for i in range(0, expected, 4):
        r, g, b, _a = data[di:di+4]
        out[i:i+4] = bytes((r, g, b, 255))
        di += 4
    return bytes(out)


def decode_alpha_only(img, bits):
    w, h = img.width, img.height
    if not (w and h):
        return None
    count = w * h
    data = img.data_bytes
    # Required bytes
    needed = (count * bits + 7) // 8
    if len(data) < needed:
        return None
    # Unpack MSB-first
    mask = (1 << bits) - 1
    out = bytearray(count * 4)
    di = 0
    oi = 0
    consumed = 0
    while consumed < count:
        byte = data[di]
        di += 1
        for shift in range(8 - bits, -1, -bits):
            if consumed >= count:
                break
            idx = (byte >> shift) & mask
            if bits == 1:
                a = 255 if idx else 0
            elif bits == 2:
                a = (idx * 85)  # 0..3 -> 0..255
            elif bits == 4:
                a = (idx * 17)  # 0..15 -> 0..255
            else:  # 8
                a = idx
            out[oi:oi+4] = bytes((255, 255, 255, a))
            oi += 4
            consumed += 1
    return bytes(out)


def decode_indexed(img, bits, depth, swap16=False):
    w, h = img.width, img.height
    if not (w and h):
        return None
    count = w * h
    colors = 1 << bits
    data = img.data_bytes
    # Determine palette stride based on depth
    if depth == '32':
        pal_stride = 4  # RGBA8888
    else:
        pal_stride = 3  # RGB565 (lo,hi) + A
    pal_bytes = colors * pal_stride
    if len(data) < pal_bytes:
        return None
    palette_raw = data[:pal_bytes]
    pixels_raw = data[pal_bytes:]
    needed_idx_bytes = (count * bits + 7) // 8
    if len(pixels_raw) < needed_idx_bytes:
        return None

    # Build palette as RGBA8888
    palette = []
    if pal_stride == 4:
        for i in range(0, pal_bytes, 4):
            r, g, b, a = palette_raw[i:i+4]
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

    # Unpack indices MSB-first
    out = bytearray(count * 4)
    oi = 0
    consumed = 0
    mask = (1 << bits) - 1
    di = 0
    while consumed < count:
        byte = pixels_raw[di]
        di += 1
        for shift in range(8 - bits, -1, -bits):
            if consumed >= count:
                break
            idx = (byte >> shift) & mask
            r, g, b, a = palette[idx]
            out[oi:oi+4] = bytes((r, g, b, a))
            oi += 4
            consumed += 1
    return bytes(out)


def rgba_bytes_to_photo(master, w, h, rgba_bytes):
    # Build a PPM (P6) in-memory; tkinter PhotoImage supports PPM via data= and format='PPM'
    # We must strip alpha by blending against black for preview; alternatively, pre-multiply on white.
    rgb = bytearray(w * h * 3)
    ri = 0
    for i in range(0, len(rgba_bytes), 4):
        r, g, b, a = rgba_bytes[i:i+4]
        # Alpha blend over transparent background (here assume black)
        # out = src since bg=0
        rgb[ri:ri+3] = bytes((r, g, b))
        ri += 3
    header = f"P6\n{w} {h}\n255\n".encode('ascii')
    ppm = header + bytes(rgb)
    return tk.PhotoImage(data=ppm, format='PPM')


class ViewerApp:
    def __init__(self, master):
        self.master = master
        master.title('LVGL Image Viewer')
        self.state_path = os.path.join(os.path.dirname(__file__), '.viewer_state.json')
        self.last_dir = os.getcwd()
        self._load_state()

        # Controls frame
        ctrl = tk.Frame(master)
        ctrl.pack(side=tk.TOP, fill=tk.X, padx=8, pady=8)

        # Row 1: Path + Open/Reload
        row1 = tk.Frame(ctrl)
        row1.pack(side=tk.TOP, fill=tk.X)
        self.path_var = tk.StringVar()
        tk.Entry(row1, textvariable=self.path_var).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))
        tk.Button(row1, text='Open .c', command=self.on_open).pack(side=tk.LEFT, padx=(0, 6))
        tk.Button(row1, text='Reload', command=self.on_reload).pack(side=tk.LEFT)

        # Row 2: Options + Actions
        row2 = tk.Frame(ctrl)
        row2.pack(side=tk.TOP, fill=tk.X, pady=(6, 0))

        self.cf_override = tk.StringVar(value='AUTO')
        tk.Label(row2, text='Format').pack(side=tk.LEFT, padx=(0, 4))
        fmt_menu = tk.OptionMenu(row2, self.cf_override, 'AUTO', *LV_IMG_CF_MAP.keys())
        fmt_menu.config(width=24)
        fmt_menu.pack(side=tk.LEFT)

        self.depth_var = tk.StringVar(value='16')
        tk.Label(row2, text='LV_COLOR_DEPTH').pack(side=tk.LEFT, padx=(12, 4))
        depth_menu = tk.OptionMenu(row2, self.depth_var, '16', '32')
        depth_menu.config(width=4)
        depth_menu.pack(side=tk.LEFT)

        self.swap_var = tk.BooleanVar(value=False)
        tk.Checkbutton(row2, text='LV_COLOR_16_SWAP', variable=self.swap_var).pack(side=tk.LEFT, padx=(12, 0))

        actions = tk.Frame(row2)
        actions.pack(side=tk.RIGHT)
        tk.Button(actions, text='Render', command=self.on_render).pack(side=tk.LEFT, padx=(0, 6))
        tk.Button(actions, text='Compare', command=self.on_compare).pack(side=tk.LEFT)

        # Info + Canvas
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
            # remember containing directory
            self.last_dir = os.path.dirname(path) or self.last_dir
            self._save_state()
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

        rgba = None
        try:
            if cf_name == 'LV_IMG_CF_TRUE_COLOR_ALPHA':
                if depth == '16':
                    rgba = decode_true_color_alpha_rgb565a(img, swap16=swap)
                else:
                    rgba = decode_true_color_alpha_rgba8888(img)
            elif cf_name == 'LV_IMG_CF_TRUE_COLOR':
                if depth == '16':
                    rgba = decode_true_color_rgb565(img, swap16=swap)
                else:
                    rgba = decode_true_color_rgba8888(img)
            elif cf_name == 'LV_IMG_CF_ALPHA_1BIT':
                rgba = decode_alpha_only(img, 1)
            elif cf_name == 'LV_IMG_CF_ALPHA_2BIT':
                rgba = decode_alpha_only(img, 2)
            elif cf_name == 'LV_IMG_CF_ALPHA_4BIT':
                rgba = decode_alpha_only(img, 4)
            elif cf_name == 'LV_IMG_CF_ALPHA_8BIT':
                rgba = decode_alpha_only(img, 8)
            elif cf_name == 'LV_IMG_CF_INDEXED_1BIT':
                rgba = decode_indexed(img, 1, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_2BIT':
                rgba = decode_indexed(img, 2, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_4BIT':
                rgba = decode_indexed(img, 4, depth, swap16=swap)
            elif cf_name == 'LV_IMG_CF_INDEXED_8BIT':
                rgba = decode_indexed(img, 8, depth, swap16=swap)
        except Exception as e:
            messagebox.showerror('Error', f'Decode failed:\n{e}')
            return

        if rgba is None:
            messagebox.showwarning('Warning', 'Unsupported format or decode failed (showing nothing).')
            return

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

        candidates = [
            'LV_IMG_CF_TRUE_COLOR_ALPHA',
            'LV_IMG_CF_TRUE_COLOR',
            'LV_IMG_CF_ALPHA_8BIT',
            'LV_IMG_CF_ALPHA_1BIT',
            'LV_IMG_CF_INDEXED_8BIT',
            'LV_IMG_CF_INDEXED_4BIT',
        ]

        win = tk.Toplevel(self.master)
        win.title('Compare Formats')
        grid = tk.Frame(win)
        grid.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        photos = []  # keep refs
        row = 0
        col = 0
        for name in candidates:
            try:
                if name == 'LV_IMG_CF_TRUE_COLOR_ALPHA':
                    rgba = decode_true_color_alpha_rgb565a(img, swap16=swap) if depth == '16' else decode_true_color_alpha_rgba8888(img)
                elif name == 'LV_IMG_CF_TRUE_COLOR':
                    rgba = decode_true_color_rgb565(img, swap16=swap) if depth == '16' else decode_true_color_rgba8888(img)
                elif name == 'LV_IMG_CF_ALPHA_1BIT':
                    rgba = decode_alpha_only(img, 1)
                elif name == 'LV_IMG_CF_ALPHA_8BIT':
                    rgba = decode_alpha_only(img, 8)
                elif name == 'LV_IMG_CF_INDEXED_8BIT':
                    rgba = decode_indexed(img, 8, depth, swap16=swap)
                elif name == 'LV_IMG_CF_INDEXED_4BIT':
                    rgba = decode_indexed(img, 4, depth, swap16=swap)
                else:
                    rgba = None
            except Exception:
                rgba = None

            frame = tk.Frame(grid, bd=1, relief=tk.SOLID)
            frame.grid(row=row, column=col, padx=6, pady=6, sticky='nsew')
            tk.Label(frame, text=name, anchor='w').pack(fill=tk.X)
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
        # Keep photos alive on window
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
            if isinstance(data, dict) and 'last_dir' in data:
                self.last_dir = data['last_dir']
        except Exception:
            pass

    def _save_state(self):
        try:
            data = {'last_dir': self.last_dir}
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



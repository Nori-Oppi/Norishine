"""Windows measurements for compare.py, measured the same way for every host and client.

- FrameWatcher: watches the visible strip of the client's stream window and notes when a
  new picture appears (time to first frame, frame pacing, recovery after loss).
- ProcessCpu / SystemCpu: CPU load from GetProcessTimes / GetSystemTimes.
- GpuEngines: per-process GPU engine load from the "GPU Engine" performance counters
  (the video encoder of the host process, the video decoder of the client process).
"""
import ctypes
import ctypes.wintypes as w
import threading
import time

user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
pdh = ctypes.WinDLL('pdh')

try:
    # Physical pixels, so window rectangles and screen captures agree
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except (AttributeError, OSError):
    pass

user32.GetDC.restype = w.HDC
user32.GetDC.argtypes = [w.HWND]
user32.ReleaseDC.argtypes = [w.HWND, w.HDC]
gdi32.CreateCompatibleDC.restype = w.HDC
gdi32.CreateCompatibleDC.argtypes = [w.HDC]
gdi32.CreateDIBSection.restype = w.HBITMAP
gdi32.CreateDIBSection.argtypes = [w.HDC, ctypes.c_void_p, w.UINT, ctypes.POINTER(ctypes.c_void_p), w.HANDLE, w.DWORD]
gdi32.SelectObject.restype = w.HGDIOBJ
gdi32.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]
gdi32.StretchBlt.argtypes = [w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                             w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.DWORD]
gdi32.SetStretchBltMode.argtypes = [w.HDC, ctypes.c_int]
gdi32.DeleteObject.argtypes = [w.HGDIOBJ]
gdi32.DeleteDC.argtypes = [w.HDC]
user32.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
user32.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [w.HWND]
user32.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user32.ClientToScreen.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]
user32.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT]
user32.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user32.GetSystemMetrics.argtypes = [ctypes.c_int]
kernel32.OpenProcess.restype = w.HANDLE
kernel32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel32.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4
kernel32.GetSystemTimes.argtypes = [ctypes.POINTER(w.FILETIME)] * 3
kernel32.CloseHandle.argtypes = [w.HANDLE]

SRCCOPY = 0x00CC0020
CAPTUREBLT = 0x40000000
WNDENUMPROC = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)


def screen_size():
    return user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)


def client_windows(pid):
    """(hwnd, class, visible, rect) of every top-level window of a process."""
    found = []

    def cb(hwnd, _):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            found.append((hwnd, name.value, bool(user32.IsWindowVisible(hwnd)), window_rect(hwnd)))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def stream_window(pid):
    """The client's visible SDL stream window (the largest, if there are several), or None."""
    wins = [(h, (r[2] - r[0]) * (r[3] - r[1])) for h, c, v, r in client_windows(pid) if v and c.startswith('SDL')]
    return max(wins, key=lambda x: x[1])[0] if wins else None


def screenshot(path):
    """Saves the primary screen as a BMP file (for failure reports)."""
    sw, sh = screen_size()
    screen = user32.GetDC(None)
    mem = gdi32.CreateCompatibleDC(screen)
    info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), sw, -sh, 1, 32, 0, 0, 0, 0, 0, 0)
    bits = ctypes.c_void_p()
    bmp = gdi32.CreateDIBSection(mem, ctypes.byref(info), 0, ctypes.byref(bits), None, 0)
    old = gdi32.SelectObject(mem, bmp)
    gdi32.StretchBlt(mem, 0, 0, sw, sh, screen, 0, 0, sw, sh, SRCCOPY | CAPTUREBLT)
    data = ctypes.string_at(bits, sw * sh * 4)
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(None, screen)
    import struct
    header = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54)
    dib = struct.pack('<IiiHHIIiiII', 40, sw, -sh, 1, 32, 0, len(data), 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(header + dib + data)


def work_area_bottom():
    r = w.RECT()
    user32.SystemParametersInfoW(0x0030, 0, ctypes.byref(r), 0)  # SPI_GETWORKAREA
    return r.bottom


def move_window(hwnd, x, y):
    """Moves a window and keeps it above other windows (HWND_TOPMOST; SWP_NOSIZE | SWP_NOACTIVATE)."""
    user32.SetWindowPos(hwnd, w.HWND(-1), x, y, 0, 0, 0x0001 | 0x0010)


def window_rect(hwnd):
    r = w.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def client_origin(hwnd):
    p = w.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    r = w.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    return p.x, p.y, r.right, r.bottom


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', w.DWORD), ('biWidth', w.LONG), ('biHeight', w.LONG), ('biPlanes', w.WORD),
                ('biBitCount', w.WORD), ('biCompression', w.DWORD), ('biSizeImage', w.DWORD),
                ('biXPelsPerMeter', w.LONG), ('biYPelsPerMeter', w.LONG), ('biClrUsed', w.DWORD),
                ('biClrImportant', w.DWORD)]


class FrameWatcher:
    """Samples a screen rectangle every `period` seconds and records when its picture changes.

    `changes` holds the perf_counter times at which a new picture was seen; `first_bright` the
    first time the area showed the test picture's many colours (the stream's first frame; a
    window without a picture yet is black or one flat colour)."""

    TW, TH = 192, 16

    def __init__(self, period=0.004):
        self.period = period
        self.rect = None
        self.changes = []
        self.first_bright = None
        self.samples = 0
        self._stop = threading.Event()
        self._thread = None

    def start(self, rect):
        self.rect = rect
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(5)

    def _run(self):
        x, y, cw, ch = self.rect
        screen = user32.GetDC(None)
        mem = gdi32.CreateCompatibleDC(screen)
        info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), self.TW, -self.TH, 1, 32, 0, 0, 0, 0, 0, 0)
        bits = ctypes.c_void_p()
        bmp = gdi32.CreateDIBSection(mem, ctypes.byref(info), 0, ctypes.byref(bits), None, 0)
        old = gdi32.SelectObject(mem, bmp)
        gdi32.SetStretchBltMode(mem, 3)  # COLORONCOLOR: fast, no averaging
        size = self.TW * self.TH * 4
        prev = None
        try:
            next_t = time.perf_counter()
            while not self._stop.is_set():
                gdi32.StretchBlt(mem, 0, 0, self.TW, self.TH, screen, x, y, cw, ch, SRCCOPY | CAPTUREBLT)
                now = time.perf_counter()
                cur = ctypes.string_at(bits, size)
                self.samples += 1
                if self.first_bright is None and len({cur[i:i + 3] for i in range(0, size, 4)}) > 40:
                    self.first_bright = now
                if prev is not None and cur != prev and self.first_bright is not None:
                    self.changes.append(now)
                prev = cur
                next_t += self.period
                delay = next_t - time.perf_counter()
                if delay > 0:
                    time.sleep(delay)
                else:
                    next_t = time.perf_counter()
        finally:
            gdi32.SelectObject(mem, old)
            gdi32.DeleteObject(bmp)
            gdi32.DeleteDC(mem)
            user32.ReleaseDC(None, screen)


def _ft(ft):
    return (ft.dwHighDateTime << 32) | ft.dwLowDateTime


class ProcessCpu:
    """CPU used by one process between start() and stop(), in percent of the whole machine."""

    def __init__(self, pid):
        self.handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        self.t0 = self.c0 = None

    def _cpu(self):
        a, b, k, u = w.FILETIME(), w.FILETIME(), w.FILETIME(), w.FILETIME()
        if not self.handle or not kernel32.GetProcessTimes(self.handle, a, b, k, u):
            return None
        return _ft(k) + _ft(u)

    def start(self):
        self.t0, self.c0 = time.perf_counter(), self._cpu()

    def stop(self):
        c1, t1 = self._cpu(), time.perf_counter()
        if self.handle:
            kernel32.CloseHandle(self.handle)
        if c1 is None or self.c0 is None:
            return None
        import os
        return round((c1 - self.c0) / 1e7 / (t1 - self.t0) / os.cpu_count() * 100, 2)


class SystemCpu:
    def _times(self):
        idle, kern, user = w.FILETIME(), w.FILETIME(), w.FILETIME()
        kernel32.GetSystemTimes(idle, kern, user)
        return _ft(idle), _ft(kern) + _ft(user)

    def start(self):
        self.i0, self.t0 = self._times()

    def stop(self):
        i1, t1 = self._times()
        total = t1 - self.t0
        return round(100 * (1 - (i1 - self.i0) / total), 2) if total else None


class PDH_FMT_COUNTERVALUE_ITEM_W(ctypes.Structure):
    class _V(ctypes.Structure):
        _fields_ = [('CStatus', w.DWORD), ('pad', w.DWORD), ('doubleValue', ctypes.c_double)]
    _fields_ = [('szName', w.LPWSTR), ('FmtValue', _V)]


class GpuEngines:
    """Samples every GPU engine's load once a second and keeps, per watched pid, the busiest
    video engine (NVIDIA/Intel "VideoEncode"/"VideoDecode", AMD "Video Codec")."""

    def __init__(self, pids):
        self.pids = {p: [] for p in pids}
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(5)
        return {p: (round(sum(v) / len(v), 2) if v else None) for p, v in self.pids.items()}

    def _run(self):
        query, counter = w.HANDLE(), w.HANDLE()
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(query)) != 0:
            return
        if pdh.PdhAddEnglishCounterW(query, '\\GPU Engine(*)\\Utilization Percentage', 0, ctypes.byref(counter)) != 0:
            return
        pdh.PdhCollectQueryData(query)
        while not self._stop.wait(1.0):
            if pdh.PdhCollectQueryData(query) != 0:
                continue
            size, count = w.DWORD(0), w.DWORD(0)
            pdh.PdhGetFormattedCounterArrayW(counter, 0x00000200, ctypes.byref(size), ctypes.byref(count), None)
            buf = (ctypes.c_byte * size.value)()
            if pdh.PdhGetFormattedCounterArrayW(counter, 0x00000200, ctypes.byref(size), ctypes.byref(count), buf) != 0:
                continue
            items = ctypes.cast(buf, ctypes.POINTER(PDH_FMT_COUNTERVALUE_ITEM_W))
            per_pid = {}
            for i in range(count.value):
                name = items[i].szName or ''
                if 'engtype_Video' not in name or 'JPEG' in name:
                    continue
                try:
                    pid = int(name.split('_')[1])
                except (IndexError, ValueError):
                    continue
                if pid in self.pids:
                    v = items[i].FmtValue.doubleValue
                    per_pid[pid] = max(per_pid.get(pid, 0.0), min(100.0, v))
            for pid in self.pids:
                self.pids[pid].append(per_pid.get(pid, 0.0))
        pdh.PdhCloseQuery(query)

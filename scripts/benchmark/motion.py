"""A window of moving, changing blocks: the same encoder load on every benchmark run.

Usage: python motion.py [seconds] [width] [height] [borderless]
Draws one new frame for every screen refresh until the time is up or the window is closed.
The pattern is seeded, so every run shows the same frames.

Windows only. Frames are paced by the desktop compositor and drawn with GDI, only the parts
that change (roadmap N16): Tk's timers fire only every 15.6 ms on Windows and drift against
the refresh, and redrawing its full-screen canvas took 11 to 12 ms, so 5 to 20 % of the
refreshes showed no new frame, a different share in every run.
"""
import ctypes
import random
import sys
import time
import tkinter
from ctypes import wintypes as w

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 90
width = int(sys.argv[2]) if len(sys.argv) > 2 else 1280
height = int(sys.argv[3]) if len(sys.argv) > 3 else 720
# Without a title bar the window covers exactly width x height from the top left (compare.py)
borderless = len(sys.argv) > 4 and sys.argv[4] == 'borderless'
COLS, ROWS = 32, 18
BALL = 120

user32, gdi32, dwmapi = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.dwmapi
user32.GetDC.argtypes, user32.GetDC.restype = [w.HWND], w.HDC
user32.ReleaseDC.argtypes = [w.HWND, w.HDC]
user32.FillRect.argtypes = [w.HDC, ctypes.POINTER(w.RECT), w.HBRUSH]
gdi32.CreateSolidBrush.argtypes, gdi32.CreateSolidBrush.restype = [w.COLORREF], w.HBRUSH
gdi32.DeleteObject.argtypes = [w.HGDIOBJ]
gdi32.SelectObject.argtypes, gdi32.SelectObject.restype = [w.HDC, w.HGDIOBJ], w.HGDIOBJ
gdi32.GetStockObject.restype = w.HGDIOBJ
gdi32.Ellipse.argtypes = [w.HDC] + [ctypes.c_int] * 4
gdi32.GdiFlush.restype = w.BOOL
NULL_PEN, WHITE_BRUSH = 8, 0


class DwmTimingInfo(ctypes.Structure):
    """DWM_TIMING_INFO (packed): only the refresh period and the last vertical blank are used."""
    _pack_ = 1
    _fields_ = [('cbSize', ctypes.c_uint32), ('rateRefresh', ctypes.c_uint32 * 2), ('qpcRefreshPeriod', ctypes.c_uint64),
                ('rateCompose', ctypes.c_uint32 * 2), ('qpcVBlank', ctypes.c_uint64), ('rest', ctypes.c_uint8 * 256)]


def qpc():
    v = ctypes.c_int64()
    ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(v))
    return v.value


def refresh_pacer(offset):
    """Returns a function that sleeps until `offset` (a fraction of a refresh) after the next
    vertical blank, at most once per refresh. Not DwmFlush: while the benchmark's watcher
    reads the screen, DwmFlush waits two refreshes every time; and
    D3DKMTWaitForVerticalBlankEvent fails on AMD's driver."""
    freq = ctypes.c_int64()
    ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
    info = DwmTimingInfo(cbSize=ctypes.sizeof(DwmTimingInfo))
    last = [0]

    def wait():
        if dwmapi.DwmGetCompositionTimingInfo(None, ctypes.byref(info)) != 0 or not info.qpcRefreshPeriod:
            time.sleep(1 / 60)
            return
        period, now = info.qpcRefreshPeriod, qpc()
        target = info.qpcVBlank + ((now - info.qpcVBlank) // period + 1) * period + int(offset * period)
        while target - last[0] < period // 2:  # one frame per refresh
            target += period
        last[0] = target
        time.sleep(max(0, target - qpc()) / freq.value)
    return wait

root = tkinter.Tk()
root.title('Norishine benchmark motion')
root.geometry('%dx%d+0+0' % (width, height))
root.attributes('-topmost', True)
if borderless:
    root.overrideredirect(True)
area = tkinter.Frame(root, width=width, height=height, bg='black')
area.pack(fill='both', expand=True)
closed = False


def close():
    global closed
    closed = True


root.protocol('WM_DELETE_WINDOW', close)
root.update()

rng = random.Random(1234)
cw, ch = width / COLS, height / ROWS
cell_rects = [(round(c * cw), round(r * ch), round((c + 1) * cw), round((r + 1) * ch))
              for r in range(ROWS) for c in range(COLS)]
colours = [None] * len(cell_rects)
hdc = user32.GetDC(area.winfo_id())
gdi32.SelectObject(hdc, gdi32.GetStockObject(NULL_PEN))


def fill(i):
    if colours[i] is None:
        return  # not set yet: the frame's black background
    rgb = colours[i]
    brush = gdi32.CreateSolidBrush(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | (rgb >> 16))  # 0xRRGGBB to COLORREF
    user32.FillRect(hdc, ctypes.byref(w.RECT(*cell_rects[i])), brush)
    gdi32.DeleteObject(brush)


def cells_under(x, y):
    c0, c1 = int(x // cw), min(COLS - 1, int((x + BALL) // cw))
    r0, r1 = int(y // ch), min(ROWS - 1, int((y + BALL) // ch))
    return {r * COLS + c for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)}


def ball_at(n):
    return (n * 7) % (width - BALL), (n * 5) % (height - BALL)


wait_vblank = refresh_pacer(0.05)
start = time.monotonic()
frame = 0
while not closed and time.monotonic() - start < seconds:
    # Drawn just after a refresh, so the compositor has the whole refresh to pick it up
    wait_vblank()
    frame += 1
    # An eighth of the cells change each frame; a ball crosses the screen.
    dirty = set()
    for _ in range(len(cell_rects) // 8):
        i = rng.randrange(len(cell_rects))
        colours[i] = rng.randrange(0x1000000)
        dirty.add(i)
    x, y = ball_at(frame)
    if frame > 1:
        dirty |= cells_under(*ball_at(frame - 1))  # where the ball was
    dirty |= cells_under(x, y)
    for i in dirty:
        fill(i)
    gdi32.SelectObject(hdc, gdi32.GetStockObject(WHITE_BRUSH))
    gdi32.Ellipse(hdc, x, y, x + BALL + 1, y + BALL + 1)  # NULL_PEN draws one pixel short
    gdi32.GdiFlush()
    try:
        root.update()  # window messages only; nothing in Tk draws
    except tkinter.TclError:  # the window was destroyed
        break

user32.ReleaseDC(area.winfo_id(), hdc)

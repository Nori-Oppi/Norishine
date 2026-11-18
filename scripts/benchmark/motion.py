"""A window of moving, changing blocks: the same encoder load on every benchmark run.

Usage: python motion.py [seconds] [width] [height] [borderless]
Draws at about 60 frames per second until the time is up or the window is closed.
The pattern is seeded, so every run shows the same frames.
"""
import random
import sys
import time
import tkinter

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 90
width = int(sys.argv[2]) if len(sys.argv) > 2 else 1280
height = int(sys.argv[3]) if len(sys.argv) > 3 else 720
# Without a title bar the window covers exactly width x height from the top left (compare.py)
borderless = len(sys.argv) > 4 and sys.argv[4] == 'borderless'
COLS, ROWS = 32, 18

root = tkinter.Tk()
root.title('Norishine benchmark motion')
root.geometry('%dx%d+0+0' % (width, height))
root.attributes('-topmost', True)
if borderless:
    root.overrideredirect(True)
canvas = tkinter.Canvas(root, width=width, height=height, highlightthickness=0, bg='black')
canvas.pack(fill='both', expand=True)

rng = random.Random(1234)
cw, ch = width / COLS, height / ROWS
cells = [canvas.create_rectangle(c * cw, r * ch, (c + 1) * cw, (r + 1) * ch, width=0)
         for r in range(ROWS) for c in range(COLS)]
ball = canvas.create_oval(0, 0, 120, 120, fill='white', width=0)
start = time.monotonic()
frame = 0
FRAME = 1 / 60


def step():
    global frame
    frame += 1
    # An eighth of the cells change each frame; a ball crosses the screen.
    for _ in range(len(cells) // 8):
        cell = cells[rng.randrange(len(cells))]
        canvas.itemconfigure(cell, fill='#%06x' % rng.randrange(0x1000000))
    x = (frame * 7) % (width - 120)
    y = (frame * 5) % (height - 120)
    canvas.coords(ball, x, y, x + 120, y + 120)
    now = time.monotonic()
    if now - start >= seconds:
        root.destroy()
        return
    # Tk timers are coarse on Windows: aim at the next frame's time, not a fixed delay.
    delay = start + frame * FRAME - now
    root.after(max(1, int(delay * 1000)), step)


root.after(1, step)
root.mainloop()

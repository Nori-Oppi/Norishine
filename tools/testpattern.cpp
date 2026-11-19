/**
 * @file tools/testpattern.cpp
 * @brief Moving test picture for the optimisation test streams (roadmap N6).
 *
 * Covers one display with seeded moving blocks over a fine pattern, redrawn every refresh,
 * so the encoder gets the same, steadily changing work on every host and every run.
 *
 *   norishine-testpattern [--seconds N] [--display \\.\DISPLAY1]
 *
 * Without --display it covers the primary display. It closes after N seconds (default 60),
 * on Escape, or when it's closed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
  struct block_t {
    double x, y, dx, dy;
    int w, h;
    COLORREF color;
  };

  RECT g_area {};
  std::vector<block_t> g_blocks;
  HBITMAP g_back = nullptr;
  HDC g_back_dc = nullptr;
  uint32_t g_frame = 0;

  // Deterministic, so every run draws the same picture
  uint32_t g_seed = 0x4e6f7269;

  uint32_t next_random() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
  }

  BOOL CALLBACK find_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto *wanted = reinterpret_cast<std::wstring *>(param);
    MONITORINFOEXW info {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) && *wanted == info.szDevice) {
      g_area = info.rcMonitor;
      wanted->clear();
      return FALSE;
    }
    return TRUE;
  }

  void make_blocks(int width, int height) {
    g_blocks.clear();
    for (int i = 0; i < 48; ++i) {
      block_t b;
      b.w = width / 20 + static_cast<int>(next_random() % (width / 8));
      b.h = height / 20 + static_cast<int>(next_random() % (height / 6));
      b.x = next_random() % (width - b.w);
      b.y = next_random() % (height - b.h);
      b.dx = (static_cast<int>(next_random() % 21) - 10) * width / 1920.0;
      b.dy = (static_cast<int>(next_random() % 15) - 7) * height / 1080.0;
      b.color = RGB(next_random() % 256, next_random() % 256, next_random() % 256);
      g_blocks.push_back(b);
    }
  }

  void draw(HDC dc, int width, int height) {
    // Fine diagonal stripes that shift each frame: detail the encoder can't skip
    HBRUSH stripe_brushes[2] = {CreateSolidBrush(RGB(40, 44, 52)), CreateSolidBrush(RGB(70, 76, 88))};
    for (int y = 0; y < height; y += 8) {
      RECT row {0, y, width, y + 8};
      FillRect(dc, &row, stripe_brushes[((y / 8) + g_frame) & 1]);
    }
    DeleteObject(stripe_brushes[0]);
    DeleteObject(stripe_brushes[1]);

    for (auto &b : g_blocks) {
      b.x += b.dx;
      b.y += b.dy;
      if (b.x < 0 || b.x + b.w > width) {
        b.dx = -b.dx;
        b.x += 2 * b.dx;
      }
      if (b.y < 0 || b.y + b.h > height) {
        b.dy = -b.dy;
        b.y += 2 * b.dy;
      }
      HBRUSH brush = CreateSolidBrush(b.color);
      RECT r {static_cast<int>(b.x), static_cast<int>(b.y), static_cast<int>(b.x) + b.w, static_cast<int>(b.y) + b.h};
      FillRect(dc, &r, brush);
      DeleteObject(brush);
    }

    // A frame counter, so every frame differs even where nothing moves
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    auto text = std::to_wstring(g_frame);
    TextOutW(dc, 16, 16, text.c_str(), static_cast<int>(text.size()));
    ++g_frame;
  }

  LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
          DestroyWindow(hwnd);
        }
        return 0;
      case WM_TIMER:
        if (wp == 2) {
          DestroyWindow(hwnd);
        }
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
  }
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int seconds = 60;
  std::wstring display;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--seconds" && i + 1 < argc) {
      seconds = _wtoi(argv[++i]);
    } else if (arg == L"--display" && i + 1 < argc) {
      display = argv[++i];
    }
  }
  LocalFree(argv);
  if (seconds <= 0) {
    seconds = 60;
  }

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  POINT origin {0, 0};
  HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info {};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(primary, &info);
  g_area = info.rcMonitor;
  if (!display.empty()) {
    EnumDisplayMonitors(nullptr, nullptr, find_monitor, reinterpret_cast<LPARAM>(&display));
  }
  int width = g_area.right - g_area.left;
  int height = g_area.bottom - g_area.top;
  if (width < 64 || height < 64) {
    return 1;
  }
  make_blocks(width, height);

  WNDCLASSW wc {};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"NorishineTestPattern";
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Norishine test pattern", WS_POPUP | WS_VISIBLE,
                              g_area.left, g_area.top, width, height, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    return 1;
  }
  SetTimer(hwnd, 2, seconds * 1000, nullptr);

  HDC window_dc = GetDC(hwnd);
  g_back_dc = CreateCompatibleDC(window_dc);
  g_back = CreateCompatibleBitmap(window_dc, width, height);
  SelectObject(g_back_dc, g_back);

  // Draws once per display refresh: DwmFlush() waits for the next composition
  MSG msg;
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        DeleteObject(g_back);
        DeleteDC(g_back_dc);
        ReleaseDC(hwnd, window_dc);
        return 0;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    draw(g_back_dc, width, height);
    BitBlt(window_dc, 0, 0, width, height, g_back_dc, 0, 0, SRCCOPY);
    if (FAILED(DwmFlush())) {
      Sleep(16);
    }
  }
}

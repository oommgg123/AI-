import ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
GetWindowTextW = user32.GetWindowTextW
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetWindowRect = user32.GetWindowRect

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)

target_pid = 20960
results = []

def cb(hwnd, lp):
    pid = wintypes.DWORD()
    GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == target_pid and IsWindowVisible(hwnd):
        buf = ctypes.create_unicode_buffer(256)
        GetWindowTextW(hwnd, buf, 256)
        rect = wintypes.RECT()
        GetWindowRect(hwnd, ctypes.byref(rect))
        results.append((hwnd, buf.value, rect.left, rect.top, rect.right, rect.bottom,
                        rect.right - rect.left, rect.bottom - rect.top))
    return True

EnumWindows(WNDENUMPROC(cb), 0)
for h, t, l, T, r, b, w, h2 in results:
    print(f"HWND={h} title='{t}' pos=({l},{T})-({r},{b}) size={w}x{h2}")
if not results:
    print("no visible windows found")

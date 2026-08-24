import ctypes, time
from ctypes import wintypes

user32 = ctypes.windll.user32

hwnd = 1707228

btn_cx, btn_cy = 22, 18
pt = wintypes.POINT(btn_cx, btn_cy)
user32.ClientToScreen(hwnd, ctypes.byref(pt))
print(f"button screen pos: ({pt.x}, {pt.y})")

user32.SetCursorPos(pt.x, pt.y)
time.sleep(0.1)

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP   = 0x0004
user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
time.sleep(0.05)
user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
print("clicked settings button")

time.sleep(2)

EnumWindows = user32.EnumWindows
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetWindowTextW = user32.GetWindowTextW
GetWindowRect = user32.GetWindowRect

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
target_pid = 20960
results = []

def cb(h, lp):
    pid = wintypes.DWORD()
    GetWindowThreadProcessId(h, ctypes.byref(pid))
    if pid.value == target_pid and IsWindowVisible(h):
        buf = ctypes.create_unicode_buffer(256)
        GetWindowTextW(h, buf, 256)
        rect = wintypes.RECT()
        GetWindowRect(h, ctypes.byref(rect))
        results.append((h, buf.value, rect.left, rect.top, rect.right, rect.bottom))
    return True

EnumWindows(WNDENUMPROC(cb), 0)
print(f"\n=== after click: {len(results)} windows ===")
for h, t, l, T, r, b in results:
    print(f"  HWND={h} title='{t}' ({l},{T})-({r},{b})")

// ============================================================================
//   共享 GDI 工具实现（见 gdi_util.h）
// ============================================================================
#include "gdi_util.h"

namespace gdi {

ATOM RegisterWindowClass(LPCWSTR className, WNDPROC wndProc, COLORREF bgColor,
                         HICON hIcon, HICON hIconSm) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(bgColor);
    wc.hIcon         = hIcon;
    wc.hIconSm       = hIconSm;
    wc.lpszClassName = className;
    return RegisterClassExW(&wc);
}

DoubleBuffer CreateDoubleBuffer(HDC hdc, int w, int h) {
    DoubleBuffer db;
    db.dc = CreateCompatibleDC(hdc);
    db.bmp = CreateCompatibleBitmap(hdc, w, h);
    db.w = w; db.h = h;
    if (db.dc && db.bmp) SelectObject(db.dc, db.bmp);
    return db;
}

void FreeDoubleBuffer(DoubleBuffer& db) {
    if (db.dc)  { DeleteDC(db.dc);  db.dc = nullptr; }
    if (db.bmp) { DeleteObject(db.bmp); db.bmp = nullptr; }
}

}  // namespace gdi

// ============================================================================
//   共享 GDI 工具（用户 2026-08-19：消除 settings / import / MC 三个 GDI 窗口
//   逐字复制的窗口类注册 + 双缓冲 DC 脚手架）
//   - RegisterWindowClass：统一注册 CS_HREDRAW|CS_VREDRAW + 默认光标 + 背景刷 + 可选图标
//   - DoubleBuffer：双缓冲内存 DC（CreateCompatibleDC / CreateCompatibleBitmap / BitBlt 封装）
// ============================================================================
#pragma once

#include <windows.h>

namespace gdi {

// 注册一个标准 GDI 窗口类。返回 RegisterClassExW 的 ATOM（失败为 0）。
// hIcon / hIconSm 传 nullptr 则窗口无图标。
ATOM RegisterWindowClass(LPCWSTR className, WNDPROC wndProc,
                         COLORREF bgColor,
                         HICON hIcon = nullptr, HICON hIconSm = nullptr);

// 在 hdc 之上创建兼容内存 DC + 位图（双缓冲）。用后调用 FreeDoubleBuffer 释放。
struct DoubleBuffer {
    HDC     dc  = nullptr;
    HBITMAP bmp = nullptr;
    int     w = 0, h = 0;
};

DoubleBuffer CreateDoubleBuffer(HDC hdc, int w, int h);
void FreeDoubleBuffer(DoubleBuffer& db);

}  // namespace gdi

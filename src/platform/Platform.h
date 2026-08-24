// ============================================================================
//  Platform 抽象层（GHOST-lite，P1：窗口创建 + Vulkan 表面）
//
//  职责：把两处 OS 边界收口到 platform:: 命名空间 ——
//    1) 窗口创建（RegisterClassExW / CreateWindowExW）
//    2) Vulkan 表面创建（vkCreateWin32SurfaceKHR）
//  使 main.cpp / vulkan_init.cpp 不再直接调用 Win32 / Vulkan-Win32 API。
//
//  未来移植 macOS / Linux：仅在 src/platform/<os>/ 下新增对应后端文件，
//  实现 platform:: 同名函数，上层零改动。
//
//  P1 仅 Windows 后端：struct Window 即 HWND 包装。
//  后续后端（macOS NSWindow* / X11 Window）将在各自平台文件内定义 Window。
// ============================================================================
#pragma once

#include <windows.h>

struct App;  // 前向声明，避免平台层反向依赖 app.h

namespace platform {

// 不透明窗口句柄。当前 Windows 后端持有 HWND。
struct Window {
    HWND hwnd = nullptr;
};

// 创建主窗口：注册窗口类 + 无边框铺满主显示器（兼容 Win7）。
// wndProc ：Windows 消息回调（P1 仍由 main.cpp::WndProc 提供；未来抽象为事件分发）。
// userData：写入 GWLP_USERDATA 的指针（通常为 &App，供 GetApp 取回）。
// 成功返回 Window*（调用方负责 DestroyMainWindow）；失败返回 nullptr 并通过 SetError 报告。
Window* CreateMainWindow(HINSTANCE hInstance,
                         const wchar_t* className,
                         const wchar_t* title,
                         WNDPROC wndProc,
                         void* userData);

// 销毁主窗口并释放 Window*。
void DestroyMainWindow(Window* w);

// 取客户区尺寸（物理像素，已含 DPI 感知）。
void GetClientSize(Window* w, int* outW, int* outH);

// 创建 Vulkan 表面（当前 Win32 表面；macOS 将走 VK_MVK_macos_surface / MoltenVK）。
// 成功写入 app.vk.surface；失败走 VKB_TRY 报错（平台层不吞错误，交上层弹窗）。
bool CreateVulkanSurface(Window* w, HINSTANCE hInstance, App& app);

// 系统版本判定：Win11 及以上返回 true（Win10 自实现边缘吸附，Win11+ 用系统原生 Snap）。
// 用 RtlGetVersion 动态取真实版本（绕过 GetVersionEx 的兼容层伪装）。
// 注意：Windows 10 = major 10；Windows 11 = major 10 且 build >= 22000（24H2 = 26100+）。
bool IsWindows11OrLater();

// ============================================================================
//  窗口操作收口（P2）：把 main.cpp 中散落的 Win32 窗口操控统一收口到 platform::，
//  全部接收 Window*（内部取 w->hwnd），未来 macOS / Linux 后端只需替换后端实现，
//  上层调用点零改动。保持零 new / 零 malloc（RECT/POINT 由调用方栈上提供）。
// ============================================================================

// 屏幕坐标 → 客户区坐标（与 swapchainExtent 同一坐标空间，DPI/多屏下安全）。
void ScreenToClient(Window* w, POINT* p);

// 取客户区矩形 / 窗口矩形（物理像素）。
void GetClientRect(Window* w, RECT* out);
void GetWindowRect(Window* w, RECT* out);

// 设置窗口位置与尺寸（flags 传 SWP_* 组合）。
void SetWindowPos(Window* w, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT flags);

// 设置显示状态：SW_SHOW / SW_MINIMIZE / SW_MAXIMIZE 等。
void ShowWindow(Window* w, int cmd);

// 前台激活（主窗口走平台收口；MC 子窗口保持原生调用）。
void SetForegroundWindow(Window* w);

// 鼠标捕获（按窗口绑定 HWND）；ReleaseCapture 为线程级全局，无需句柄。
void SetCapture(Window* w);
void ReleaseCapture();

// 触发客户区重绘（主窗口走平台收口；MC 子窗口保持原生调用）。
void InvalidateRect(Window* w, const RECT* r, BOOL erase);

// 是否最大化（zoomed）。
bool IsZoomed(Window* w);

// 关闭窗口：仅销毁 HWND，不释放 Window*（Window* 由 DestroyMainWindow 统一释放）。
void DestroyWindow(Window* w);

// 取窗口所在显示器矩形：outMonitor=整屏 rcMonitor；outWork=工作区 rcWork（避开任务栏）。
// 任一 out 传 nullptr 则跳过写入。
void GetWindowMonitorRect(Window* w, RECT* outMonitor, RECT* outWork);

// 取主显示器矩形：outMonitor=整屏 rcMonitor；outWork=工作区 rcWork。
void GetPrimaryMonitorRect(RECT* outMonitor, RECT* outWork);

}  // namespace platform

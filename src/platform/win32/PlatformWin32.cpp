// ============================================================================
//  Platform Windows 后端（P1）
//  实现 platform::CreateMainWindow / DestroyMainWindow / GetClientSize /
//  CreateVulkanSurface，封装 Win32 与 Vulkan-Win32 调用。
// ============================================================================
#include "platform/Platform.h"
#include "awa_internal.h"   // App 全定义、SetError、VKB_TRY（间接含 app.h / vulkan_loader.h）
#include "vulkan_loader.h"  // g_pfnCreateWin32SurfaceKHR

namespace platform {

Window* CreateMainWindow(HINSTANCE hInstance,
                         const wchar_t* className,
                         const wchar_t* title,
                         WNDPROC wndProc,
                         void* userData) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) {
        SetError("窗口类注册失败");
        return nullptr;
    }

    // 无边框窗口：铺满主显示器【工作区 rcWork】（自绘顶栏，无系统标题栏）。
    //
    // ★ 修复「打开即无响应、只能任务管理器关闭」的真正根因（P1 跨平台重构回归）：
    //   重构前 main.cpp 用的是 WS_OVERLAPPEDWINDOW（系统标题栏 + 最小化/关闭按钮），
    //   搬进平台层时误改成裸 WS_POPUP + rcMonitor，同时丢了三个关键位，导致：
    //     1) 用 rcMonitor（整屏，含任务栏区域）铺满 → 任务栏被完全遮住，
    //        用户只能按 Win 键才能脱离 → 误判为「软件卡死」（其实一直在正常渲染）；
    //     2) 缺 WS_SYSMENU    → Alt+F4 失效，无法用键盘关闭；
    //     3) 缺 WS_MINIMIZEBOX → ShowWindow(SW_MINIMIZE) 无效，顶栏最小化按钮点了没反应；
    //     4) 缺 WS_EX_APPWINDOW（纯 WS_POPUP 且无 owner）→ 任务栏不显示程序按钮，
    //        无法切换/最小化/右键关闭 → 只剩任务管理器强杀这一条路。
    //   现改为 rcWork（任务栏始终可见可点）+ 补齐 SYSMENU/MINIMIZEBOX/EX_APPWINDOW，
    //   外观仍是无边框自绘顶栏，但退出路径（顶栏关闭按钮 / Alt+F4 / 任务栏右键）全部可用。
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& mr = mi.rcWork;   // 工作区：避开任务栏（原为 rcMonitor 整屏 → 遮挡任务栏）

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, className, title,
                                WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        SetError("窗口创建失败");
        return nullptr;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userData));

    Window* w = new Window();
    w->hwnd = hwnd;
    return w;
}

void DestroyMainWindow(Window* w) {
    if (!w) return;
    if (w->hwnd) ::DestroyWindow(w->hwnd);
    delete w;
}

void GetClientSize(Window* w, int* outW, int* outH) {
    if (!w || !w->hwnd) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return;
    }
    RECT rc{};
    GetClientRect(w->hwnd, &rc);
    if (outW) *outW = rc.right - rc.left;
    if (outH) *outH = rc.bottom - rc.top;
}

bool CreateVulkanSurface(Window* w, HINSTANCE hInstance, App& app) {
    if (!w || !w->hwnd) {
        SetError("窗口句柄无效，无法创建 Vulkan 表面");
        return false;
    }
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = hInstance;
    surfaceInfo.hwnd = w->hwnd;
    VKB_TRY(g_pfnCreateWin32SurfaceKHR(app.vk.instance, &surfaceInfo, nullptr, &app.vk.surface));
    return true;
}

bool IsWindows11OrLater() {
    // RTL_OSVERSIONINFOW 布局（size/major/minor/build/platformId/csd），独立声明避免依赖版本头
    struct OsVer {
        ULONG size;
        ULONG major;
        ULONG minor;
        ULONG build;
        ULONG platformId;
        WCHAR csd[128];
    };
    typedef LONG(WINAPI* RtlGetVersionFn)(OsVer*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!fn) return false;
    OsVer ov{};
    ov.size = sizeof(ov);
    if (fn(&ov) != 0) return false;
    // Windows 10 = major 10；Win11 = major 10 且 build >= 22000（24H2 = 26100+）
    return (ov.major > 10) || (ov.major == 10 && ov.build >= 22000);
}

// ============================================================================
//  窗口操作收口（P2）：Window* → HWND 内部转发，上层不再依赖 Win32 细节。
//  全部判空；零 new / 零 malloc（RECT/POINT 由调用方栈上提供）。
// ============================================================================
void ScreenToClient(Window* w, POINT* p) {
    if (w && w->hwnd && p) ::ScreenToClient(w->hwnd, p);
}

void GetClientRect(Window* w, RECT* out) {
    if (w && w->hwnd && out) ::GetClientRect(w->hwnd, out);
}

void GetWindowRect(Window* w, RECT* out) {
    if (w && w->hwnd && out) ::GetWindowRect(w->hwnd, out);
}

void SetWindowPos(Window* w, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT flags) {
    if (w && w->hwnd) ::SetWindowPos(w->hwnd, hWndInsertAfter, x, y, cx, cy, flags);
}

void ShowWindow(Window* w, int cmd) {
    if (w && w->hwnd) ::ShowWindow(w->hwnd, cmd);
}

void SetForegroundWindow(Window* w) {
    if (w && w->hwnd) ::SetForegroundWindow(w->hwnd);
}

void SetCapture(Window* w) {
    if (w && w->hwnd) ::SetCapture(w->hwnd);
}

void ReleaseCapture() {
    ::ReleaseCapture();
}

void InvalidateRect(Window* w, const RECT* r, BOOL erase) {
    if (w && w->hwnd) ::InvalidateRect(w->hwnd, r, erase);
}

bool IsZoomed(Window* w) {
    return (w && w->hwnd) ? (::IsZoomed(w->hwnd) != 0) : false;
}

void DestroyWindow(Window* w) {
    // 仅销毁 HWND；Window* 的生命周期由 DestroyMainWindow 负责（避免重复 delete）。
    if (w && w->hwnd) ::DestroyWindow(w->hwnd);
}

void GetWindowMonitorRect(Window* w, RECT* outMonitor, RECT* outWork) {
    if (!w || !w->hwnd) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (outMonitor) *outMonitor = mi.rcMonitor;
        if (outWork)    *outWork    = mi.rcWork;
    }
}

void GetPrimaryMonitorRect(RECT* outMonitor, RECT* outWork) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        if (outMonitor) *outMonitor = mi.rcMonitor;
        if (outWork)    *outWork    = mi.rcWork;
    }
}

}  // namespace platform

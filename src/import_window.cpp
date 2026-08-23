// ============================================================================
//   导入窗口（用户 180 轮：500×700 默认色；GDI 双缓冲，模态）
//   用户 181 轮修复：
//   - 点击"导入 3D 模型…"→ 以导入窗口为 owner 弹文件对话框，**窗口保持打开**；
//     选好文件才置确认标志并关闭（取消则窗口继续，可再点）
//   用户 183+ 轮：
//   - 各按钮改用共享 GDI 按钮渲染管线（ui_button.h）：
//     UiButton + ButtonStateMachine 状态机 + ButtonTheme 主题 + 动画，
//     完整流程 Normal→Hover→Pressed→Released→onClick，全程平滑过渡。
//   - 松开在按钮内不直接弹框，延迟 0.1s 让回弹动画播完再触发（Round185 修复）。
//   用户 186 轮：新增第二个按钮"地图导入(be)"（选目录），走同一管线。
// ============================================================================
#include "import_window.h"

#include <windows.h>

#include <string>

#include "import_pipeline.h"
#include "ui_button.h"   // 共享 GDI 按钮渲染管线
#include "gdi_util.h"    // 共享 GDI 工具：RegisterWindowClass / DoubleBuffer（消除逐字复制的脚手架）
#include "ui_presets.h"  // 统一控件预设体系：颜色/圆角从 ui::g_theme 取值

namespace {

constexpr wchar_t kImportWndClass[] = L"awaImportWindow";
constexpr wchar_t kImportWndTitle[] = L"awa - 导入";
constexpr int kWidth  = 500;
constexpr int kHeight = 700;
const COLORREF kBgColor      = ui::g_theme.palette.bg;      // 默认色（与设置窗口一致）
const COLORREF kTextSel      = ui::g_theme.palette.text;    // 标题/按钮文字
const COLORREF kTextDim      = ui::g_theme.palette.textDim; // 说明文字
constexpr int kTimerId = 1;
constexpr int kClickTimerId = 2;   // 松开后延迟 0.1s 再弹框，让回弹动画播完
constexpr int kClickDelayMs = 100;
// 禁止缩放/最小化/最大化：去掉 WS_THICKFRAME(可拖拽缩放边框)、WS_MINIMIZEBOX、WS_MAXIMIZEBOX，
// 仅保留标题栏 + 系统菜单(关闭按钮)。窗口尺寸由 WM_GETMINMAXINFO 锁死为固定大小。
constexpr DWORD kImportStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

// 两个按钮的布局（VkRect2D，与共享管线一致）
constexpr int kBtnX   = 60;
constexpr int kBtnW   = kWidth - 120;
constexpr int kBtnH   = 42;
constexpr int kBtnY3D = 120;   // "导入 3D 模型…"
constexpr int kBtnYMC = 180;   // "地图导入(be)"

VkRect2D Btn3DRect() { return {{kBtnX, kBtnY3D}, {static_cast<uint32_t>(kBtnW), static_cast<uint32_t>(kBtnH)}}; }
VkRect2D BtnMCRect() { return {{kBtnX, kBtnYMC}, {static_cast<uint32_t>(kBtnW), static_cast<uint32_t>(kBtnH)}}; }

HWND g_importWindow = nullptr;
HWND g_ownerWnd = nullptr;
HFONT g_font = nullptr;
gdi::DoubleBuffer g_db{};   // 双缓冲 DC（惰性缓存，尺寸变化时重建）

// 共享按钮管线状态（每个按钮一个 UiButton + 一套主题）
UiButton  g_btn3D;
UiButton  g_btnMC;

// #214：统一经 app.h 的 FromUiButtonPreset 派生（单主题源 = ui::g_theme.button）
ButtonTheme MakeButtonThemeFromPreset() {
    return FromUiButtonPreset(ui::g_theme.button);
}
ButtonTheme g_btnTheme = MakeButtonThemeFromPreset();
bool g_3dPressed = false;   // "导入 3D 模型…"是否处于"按下未释放"
bool g_mcPressed = false;   // "地图导入(be)"是否处于"按下未释放"

// 待触发的延迟动作（松开后延迟 0.1s 执行，避免阻塞动画）
enum class PendingAction { None, Import3D, McWorld };
PendingAction g_pending = PendingAction::None;

// 关闭导入窗口（正确顺序：先恢复 owner 再销毁窗口，避免激活权跳到后面的软件）
void CloseImportWindowProper(HWND hwnd);

// 仅当动画未收敛时持续重绘（收敛后停表省 CPU）
void KickAnim(HWND hwnd) {
    SetTimer(hwnd, kTimerId, 16, nullptr);
}

// 文件选择框：支持 .mcworld 归档 或 世界目录内的 level.dat
bool PickMcFile(HWND owner, wchar_t* outPath, size_t outCap) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    // 我的世界存档：同一过滤器同时显示 .mcworld 与 .dat 两种类型；未开启 MULTISELECT，仅能导入一个文件
    ofn.lpstrFilter = L"我的世界存档 (*.mcworld;*.dat)\0*.mcworld;*.dat\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = (DWORD)outCap;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    outPath[0] = L'\0';
    return GetOpenFileNameW(&ofn) != 0;
}

// 完整流程①："导入 3D 模型…" → 选文件成功 → 置确认标志 → 关窗（恢复主窗口统一在关闭函数）
void DoImportFlow(HWND hwnd) {
    wchar_t path[MAX_PATH];
    if (PickModelFile(hwnd, path, MAX_PATH)) {
        g_importPath = path;
        g_importWindowConfirm = true;
        CloseImportWindowProper(hwnd);
    }
    // 取消：窗口保持打开，可再次点击
}

// 完整流程②："地图导入(be)" → 选 .mcworld/level.dat → 关闭导入窗口 → 交由主循环处理
// （Validate 含 zip 解压，移到主循环避免阻塞 UI + 弹 PowerShell 窗口）
void DoMcWorldFlow(HWND hwnd) {
    wchar_t path[MAX_PATH];
    if (PickMcFile(hwnd, path, MAX_PATH)) {
        g_mcWorldPath = path;
        g_mcWorldConfirm = true;
        CloseImportWindowProper(hwnd);
    }
    // 取消：窗口保持打开，可再次点击
}

// 恢复主窗口交互（任何关闭路径都必须调用，避免主窗口永久模态）
void RestoreOwner() {
    if (g_ownerWnd && IsWindow(g_ownerWnd)) {
        EnableWindow(g_ownerWnd, TRUE);
        SetForegroundWindow(g_ownerWnd);
    }
    g_ownerWnd = nullptr;
}

// 关闭导入窗口（正确顺序：先恢复 owner 再销毁窗口）。
// 若在 WM_DESTROY 里才恢复 owner，系统销毁活动窗口时选择下一个激活窗口会跳过
// 仍被禁用的主窗口，把激活权交给后面的软件（突然蹦到前台）。
void CloseImportWindowProper(HWND hwnd) {
    HWND owner = g_ownerWnd;
    if (owner && IsWindow(owner)) EnableWindow(owner, TRUE);
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
    if (owner && IsWindow(owner)) SetForegroundWindow(owner);
    g_ownerWnd = nullptr;
}

LRESULT CALLBACK ImportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        if (!g_font) {
            g_font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
        }
        // 初始化共享按钮管线状态（两个按钮）
        g_btn3D.rect = Btn3DRect(); g_btn3D.radius = static_cast<float>(ui::g_theme.spacing.corner);
        g_btnMC.rect = BtnMCRect(); g_btnMC.radius = static_cast<float>(ui::g_theme.spacing.corner);
        g_btn3D.machine.state = ButtonState::Normal;
        g_btnMC.machine.state = ButtonState::Normal;
        for (int i = 0; i < 4; ++i) {
            g_btn3D.color[i]  = g_btnTheme.normal[i];
            g_btn3D.border[i] = g_btnTheme.normal[i];
            g_btnMC.color[i]  = g_btnTheme.normal[i];
            g_btnMC.border[i] = g_btnTheme.normal[i];
        }
        g_3dPressed = false;
        g_mcPressed = false;
        g_pending   = PendingAction::None;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT full;
        GetClientRect(hwnd, &full);
        const int cw = full.right - full.left;
        const int ch = full.bottom - full.top;
        // 双缓冲（防闪烁）：惰性缓存 DC（尺寸变化时重建）
        if (!g_db.dc || g_db.w != cw || g_db.h != ch) {
            gdi::FreeDoubleBuffer(g_db);
            g_db = gdi::CreateDoubleBuffer(hdc, cw, ch);
        }
        HDC memDc = g_db.dc;

        HBRUSH bg = CreateSolidBrush(kBgColor);
        FillRect(memDc, &full, bg);
        DeleteObject(bg);

        SelectObject(memDc, g_font);
        SetBkMode(memDc, TRANSPARENT);
        // 标题
        SetTextColor(memDc, kTextSel);
        RECT t{ 18, 16, kWidth - 18, 60 };
        DrawTextW(memDc, L"awa - 导入", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // 说明
        SetTextColor(memDc, kTextDim);
        RECT sub{ 18, 66, kWidth - 18, 96 };
        DrawTextW(memDc, L"选择要导入的内容：", -1, &sub, DT_LEFT | DT_TOP | DT_SINGLELINE);

        // 两个按钮：走共享 GDI 按钮渲染管线（圆角矩形 + 主题填充 + hover 高亮描边 + 文字）
        DrawGdiButton(memDc, g_btn3D, g_btnTheme, L"导入 3D 模型…",        14, g_font, kTextSel);
        DrawGdiButton(memDc, g_btnMC, g_btnTheme, L"地图导入(be)", 14, g_font, kTextSel);

        BitBlt(hdc, 0, 0, cw, ch, memDc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        // 取消尚未触发的延迟动作（防止重复弹框）
        KillTimer(hwnd, kClickTimerId);
        g_pending = PendingAction::None;
        // 完整流程①：进入 Pressed（背景变深，动画过渡）
        if (PointInButton(g_btn3D, static_cast<float>(x), static_cast<float>(y))) {
            g_btn3D.machine.OnMouseDown(true);
            g_3dPressed = true;
        } else if (PointInButton(g_btnMC, static_cast<float>(x), static_cast<float>(y))) {
            g_btnMC.machine.OnMouseDown(true);
            g_mcPressed = true;
        } else {
            return 0;
        }
        KickAnim(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        // 完整流程②：释放 → Released → 延迟触发动作（仅在按钮内释放才生效）
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        if (g_3dPressed) {
            g_3dPressed = false;
            const bool inside = PointInButton(g_btn3D, static_cast<float>(x), static_cast<float>(y));
            g_btn3D.machine.OnMouseUp(inside);
            if (inside) { g_pending = PendingAction::Import3D; SetTimer(hwnd, kClickTimerId, kClickDelayMs, nullptr); }
        } else if (g_mcPressed) {
            g_mcPressed = false;
            const bool inside = PointInButton(g_btnMC, static_cast<float>(x), static_cast<float>(y));
            g_btnMC.machine.OnMouseUp(inside);
            if (inside) { g_pending = PendingAction::McWorld; SetTimer(hwnd, kClickTimerId, kClickDelayMs, nullptr); }
        }
        KickAnim(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // 悬停态：移入→Hover（边缘白条），移出→Normal
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        const bool i3 = PointInButton(g_btn3D, static_cast<float>(x), static_cast<float>(y));
        const bool im = PointInButton(g_btnMC, static_cast<float>(x), static_cast<float>(y));
        const ButtonState b3 = g_btn3D.machine.state;
        const ButtonState bm = g_btnMC.machine.state;
        g_btn3D.machine.OnMouseMove(i3);
        g_btnMC.machine.OnMouseMove(im);
        if (g_btn3D.machine.state != b3 || g_btnMC.machine.state != bm) {
            KickAnim(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTimerId) {
            // 每帧推进两个按钮的动画
            UpdateButtonColor(g_btn3D, g_btnTheme);
            UpdateButtonColor(g_btnMC, g_btnTheme);
            // 任一按钮仍在动画（非 Normal 或颜色未收敛到 normal 目标）则继续重绘，否则停表
            bool moving = (g_btn3D.machine.state != ButtonState::Normal) ||
                          (g_btnMC.machine.state != ButtonState::Normal);
            if (!moving) {
                for (int i = 0; i < 4; ++i) {
                    const float n = g_btnTheme.normal[i];
                    if (g_btn3D.color[i]  > n + 0.01f || g_btn3D.color[i]  < n - 0.01f) moving = true;
                    if (g_btn3D.border[i]> n + 0.01f || g_btn3D.border[i] < n - 0.01f) moving = true;
                    if (g_btnMC.color[i]  > n + 0.01f || g_btnMC.color[i]  < n - 0.01f) moving = true;
                    if (g_btnMC.border[i]> n + 0.01f || g_btnMC.border[i] < n - 0.01f) moving = true;
                }
            }
            if (moving) InvalidateRect(hwnd, nullptr, FALSE);
            else KillTimer(hwnd, kTimerId);
            return 0;
        }
        if (wParam == kClickTimerId) {
            // 延迟动作：按钮回弹动画已播完，现在才触发对应流程
            KillTimer(hwnd, kClickTimerId);
            PendingAction act = g_pending;
            g_pending = PendingAction::None;
            if (act == PendingAction::Import3D)      DoImportFlow(hwnd);
            else if (act == PendingAction::McWorld)  DoMcWorldFlow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_GETMINMAXINFO: {
        // 锁定窗口尺寸，禁止任何方式缩放（配合去掉 WS_THICKFRAME）
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT r{ 0, 0, kWidth, kHeight };
        AdjustWindowRect(&r, kImportStyle, FALSE);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        mmi->ptMaxTrackSize.x = r.right - r.left;
        mmi->ptMaxTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_CLOSE:
        CloseImportWindowProper(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        KillTimer(hwnd, kClickTimerId);
        gdi::FreeDoubleBuffer(g_db);  // 释放缓存的双缓冲 DC
        RestoreOwner();
        g_importWindow = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

bool g_importWindowConfirm = false;
std::wstring g_importPath;
bool g_mcWorldConfirm = false;
std::wstring g_mcWorldPath;

HWND OpenImportWindow(HWND owner) {
    if (g_importWindow && IsWindow(g_importWindow)) {
        SetForegroundWindow(g_importWindow);
        return g_importWindow;
    }
    g_importWindowConfirm = false;
    g_mcWorldConfirm = false;   // 打开时重置，避免残留上次的确认
    HICON hAppIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));  // 复用主程序图标
    static bool registered = false;
    if (!registered) {
        // 复用共享 GDI 窗口类注册（消除逐字复制的 WNDCLASSEXW 脚手架）
        gdi::RegisterWindowClass(kImportWndClass, ImportWndProc, kBgColor, hAppIcon, hAppIcon);
        registered = true;
    }
    RECT rc{ 0, 0, kWidth, kHeight };
    AdjustWindowRect(&rc, kImportStyle, FALSE);
    HWND wnd = CreateWindowExW(0, kImportWndClass, kImportWndTitle,
                               kImportStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rc.right - rc.left, rc.bottom - rc.top,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!wnd) return nullptr;
    if (hAppIcon) {
        SendMessageW(wnd, WM_SETICON, ICON_BIG,   (LPARAM)hAppIcon);
        SendMessageW(wnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }
    g_importWindow = wnd;
    g_ownerWnd = owner;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    return wnd;
}

void CloseImportWindow() {
    if (g_importWindow && IsWindow(g_importWindow))
        CloseImportWindowProper(g_importWindow);
    else
        g_importWindow = nullptr;
}

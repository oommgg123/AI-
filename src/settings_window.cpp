// ============================================================================
//
// ============================================================================
#include "settings_window.h"

#include <algorithm>
#include <cmath>

#include "ui2d.h"    // 共享 GDI 工具：RegisterWindowClass / DoubleBuffer（消除逐字复制的脚手架）
#include "ui_presets.h"  // 统一控件预设体系：所有样式参数（颜色/圆角/间距/控件尺寸）从 ui::g_theme 取值
#include "ui_controls.h"  // 顺位布局 / 控件管理器（专用有序布局）
#include "awa_internal.h"  // ApplyAAMode / SaveSettingInt / SensToFactor（事件驱动直写 App）

namespace {

constexpr wchar_t kSettingsClassName[] = L"SettingsWindow";
constexpr wchar_t kSettingsTitle[]     = L"设置";

// ---- 统一控件预设体系（ui_presets.h）：样式参数（颜色/圆角/间距/控件尺寸）一律从
//      ui::g_theme 预设取值，禁止再硬编码。以下为运行时常量（预设 g_theme 为 inline
//      变量，非 constexpr）；纯布局类常量（kContainerW/kTrackW/kLabelW/kDampY 等）保留。----
const COLORREF kDefaultPanelRgb = ui::g_theme.palette.bg;        // 窗口背景/类注册背景
const COLORREF kButtonColor     = ui::g_theme.button.normal;     // 下拉 header / 默认填充
const COLORREF kRowColor        = ui::g_theme.palette.bg;        // 列表未选中行
const COLORREF kSelectedColor   = ui::g_theme.list.selected;     // 列表选中行（accent）
const COLORREF kBorderDim       = ui::g_theme.dropdown.border;   // 行/header 描边
const COLORREF kTextSel         = ui::g_theme.palette.text;      // 正文/选中文字
const COLORREF kTextDim         = ui::g_theme.palette.textDim;   // 次要文字
const COLORREF kListBgColor     = ui::g_theme.dropdown.bg;       // 列表整体容器背景
const COLORREF kScrollTrack     = ui::g_theme.slider.track;      // 滚动条轨道
const COLORREF kScrollThumb     = ui::g_theme.palette.scrollBar; // 滚动条滑块
const COLORREF kTrackColor      = ui::g_theme.slider.track;      // 滑条轨道底色
const COLORREF kTrackFill       = ui::g_theme.slider.fill;       // 滑条已填充段（accent）

// 两色线性混合（t∈[0,1]，0=全 a，1=全 b）
static COLORREF BlendColor(COLORREF a, COLORREF b, float t) {
    return RGB(
        static_cast<BYTE>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
        static_cast<BYTE>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
        static_cast<BYTE>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}
// 列表未选中行被鼠标触碰时的加深底色：行色→选中色之间取 ~40%，保证"加深但不强于选中"
const COLORREF kHoverRowColor = BlendColor(kRowColor, kSelectedColor, 0.40f);
const COLORREF kThumbRing       = ui::g_theme.slider.knobBorder; // 滑块外圈
const COLORREF kThumbWhite      = ui::g_theme.slider.knob;       // 滑块纯白
const COLORREF kYellowBorder    = RGB(255, 215, 26);             // 列表选中项 1px 黄边

// ---- 抗锯齿下拉列表布局（"现代化自定义外观"，用户 172 轮：整体容器背景 + 可滚动）----
const int kPad        = ui::g_theme.spacing.padX;   // 外边距（原 10 → 预设 12）
const int kItemH      = ui::g_theme.dropdown.itemH; // 行高（原 30 → 预设 26）
constexpr int kItemGap    = 1;   // 行间间隙（预设未覆盖，保留）
constexpr int kContainerW = 300; // 列表宽度（布局尺寸，不接预设）
const int kCornerD    = ui::g_theme.spacing.corner; // 圆角半径（=6）
const int kTextPad    = ui::g_theme.spacing.padX;   // 文字左内边距（=12）
constexpr int kMaxVisible = 5;   // 列表容器固定显示行数（最低承受 5 个内容）
constexpr int kScrollbarW = 8;   // 滚动条宽度（无对应预设字段，保留）
constexpr int kAACount    = 5;   // AA 模式数量（可增长：>5 时列表右侧自动出现滚动条）

// ---- 摄像机阻尼滑动条布局（用户 168 轮："自定义现代化滑动条"；169 轮整体上移 100px）----
constexpr int kLabelW = 190;   // 左侧标签"摄像机当前阻尼： **"宽度
constexpr int kTrackW = 300;   // 轨道宽度（布局尺寸，不接预设）
const int kTrackH     = ui::g_theme.slider.h;     // 轨道高度（原 6 → 预设 8）
const int kThumbR     = ui::g_theme.slider.knobR; // 圆形滑块半径（原 12 → 预设 9）
// ---- 鼠标滑动灵敏度滑动条布局（用户 2026-08-20：在摄像机阻尼下方）----

constexpr const wchar_t* kAANames[5] = {
    L"无（关闭抗锯齿）",
    L"MSAA 2x（多重采样）",
    L"MSAA 4x（多重采样）",
    L"FXAA（快速近似抗锯齿）",
    L"SSAA（超采样 4x）",
};

HWND g_settingsWindow = nullptr;
HWND g_owner = nullptr;
App* g_settingsApp = nullptr;     // 当前设置窗口持有的 App 引用（事件驱动直写，替代每帧轮询全局）

// 从 App 当前值读取设置窗口显示所需的整数（0~100 / 模式索引）
static int CurAa()   { return g_settingsApp ? static_cast<int>(g_settingsApp->aa.aaMode) : 3; }
static int CurDamp() { return g_settingsApp ? static_cast<int>(g_settingsApp->camera.damping * 100.0f + 0.5f) : 5; }
static int CurSens() { return g_settingsApp ? FactorToSens(g_settingsApp->camera.orbitSensitivity) : 50; }

HFONT g_font = nullptr;           // 微软雅黑（缓存，避免每次重绘创建）

// Round372：按钮 hover 状态（-1=无，0=header，1..kMaxVisible=opt0..optN-1）——与主窗口按钮管线对齐
int g_hoverButton = -1;
bool g_mouseTracked = false;       // TrackMouseEvent 已发起标记（防重复发起）

// 缓存的 GDI 对象（WM_CREATE 创建、WM_DESTROY 释放），避免 WM_PAINT 每次重绘反复创建/销毁 pen/brush/font
HPEN   g_borderPen = nullptr;
HBRUSH g_btnBrush  = nullptr;
HPEN   g_arrowPen  = nullptr;
HBRUSH g_arrowBrush= nullptr;
HPEN   g_trackPen  = nullptr;
HBRUSH g_trackBrush= nullptr;
HBRUSH g_fillBrush = nullptr;
HBRUSH g_ringBrush = nullptr;
HBRUSH g_whiteBrush= nullptr;
HBRUSH g_listBg    = nullptr;
HBRUSH g_scrollTrackBrush = nullptr;
HBRUSH g_scrollThumbBrush = nullptr;
HFONT  g_smallFont = nullptr;
HBRUSH g_bgBrush   = nullptr;
// 双缓冲 DC（惰性缓存，尺寸变化时重建）
ui2d::DoubleBuffer g_db{};
bool g_listOpen = false;
bool g_dragging = false;          // 阻尼滑块拖动中（SetCapture 后跟随鼠标）
bool g_draggingSens = false;      // 鼠标灵敏度滑块拖动中（SetCapture 后跟随鼠标）
int g_scrollOffset = 0;           // 列表滚动偏移（0=最顶；内容 > kMaxVisible 时有效）
bool g_scrollDragging = false;    // 列表滚动条 thumb 拖动中
int g_scrollDragStartY = 0;       // 拖动开始鼠标 y
int g_scrollDragStartOffset = 0;  // 拖动开始滚动偏移
// 列表展开/收起动画（用户 174 轮：采用主窗口菜单 menuAnim 同款——时间 lerp 平滑展开）
float g_listAnim = 0.0f;          // 0~1（0=收起，1=完全展开）
float g_listAnimFrom = 0.0f;      // 动画起始值
ULONGLONG g_listAnimStartMs = 0;  // 动画起始时间
bool g_listAnimOpen = false;      // 动画目标（true=展开）
constexpr UINT_PTR kAnimTimerId = 1;
constexpr float kListExpandSec   = 0.075f;  // 展开时长 75ms（用户 175 轮：加速 2 倍，原 150ms）
constexpr float kListCollapseSec = 0.06f;   // 收起时长 60ms（用户 175 轮：加速 2 倍，原 120ms）

// ---- 顺位布局：控件管理器（ui_controls）+ 单一来源 rect ----
static ui::ControlManager g_mgr;
enum {
    ID_HEADER = 1, ID_LIST, ID_DAMP_ROW, ID_SENS_ROW, ID_SPACER, ID_BOTTOM,
    ID_DAMP_LABEL, ID_DAMP_TRACK, ID_SENS_LABEL, ID_SENS_TRACK
};

// 顺位布局：每次需要坐标时重建注册并算力（~10 控件，开销可忽略）。规则：
//  - 主组"settings"竖向 = header → list → 阻尼行 → 灵敏度行 → 弹性 spacer → 底部文字
//  - 阻尼行/灵敏度行是横向子组（label 固定宽 + track 弹性填满）
//  - list 高度随展开动画 g_listAnim 渐变；关闭时高度 0（顺位自动收拢后续控件）
static void RecomputeSettingsLayout(HWND hwnd) {
    RECT cr; GetClientRect(hwnd, &cr);
    RECT area = cr; InflateRect(&area, -kPad, -kPad);   // 外边距
    const int maxH  = kMaxVisible * (kItemH + kItemGap);
    const int listH = (g_listOpen || g_listAnim > 0.001f)
        ? std::max(1, static_cast<int>(maxH * g_listAnim + 0.5f)) : 0;
    g_mgr.reset();
    // 主组（竖向）
    g_mgr.add({"settings", 0, ui::Axis::Vertical,   ui::Anchor::None, kPad, kItemH,      kContainerW,       0, ID_HEADER});
    g_mgr.add({"settings", 1, ui::Axis::Vertical,   ui::Anchor::None, 0,    listH,        kContainerW,       0, ID_LIST});
    g_mgr.add({"settings", 2, ui::Axis::Vertical,   ui::Anchor::None, 12,   2*kThumbR+8,  kLabelW + kTrackW, 0, ID_DAMP_ROW,  false, "damp-row"});
    g_mgr.add({"settings", 3, ui::Axis::Vertical,   ui::Anchor::None, 8,    2*kThumbR+8,  kLabelW + kTrackW, 0, ID_SENS_ROW,  false, "sens-row"});
    g_mgr.add({"settings", 4, ui::Axis::Vertical,   ui::Anchor::None, 0,    -1,           -1,               1, ID_SPACER,    true});
    g_mgr.add({"settings", 5, ui::Axis::Vertical,   ui::Anchor::None, 0,    24,           -1,               0, ID_BOTTOM});
    // 阻尼行（横向）
    g_mgr.add({"damp-row", 0, ui::Axis::Horizontal, ui::Anchor::None, 0,    kLabelW,      -1,               0, ID_DAMP_LABEL});
    g_mgr.add({"damp-row", 1, ui::Axis::Horizontal, ui::Anchor::None, 0,    -1,           -1,               1, ID_DAMP_TRACK});
    // 灵敏度行（横向）
    g_mgr.add({"sens-row", 0, ui::Axis::Horizontal, ui::Anchor::None, 0,    kLabelW,      -1,               0, ID_SENS_LABEL});
    g_mgr.add({"sens-row", 1, ui::Axis::Horizontal, ui::Anchor::None, 0,    -1,           -1,               1, ID_SENS_TRACK});
    g_mgr.compute(area);
}

// 安全取控件 rect 封装
static const ui::Ctrl* C(const std::string& g, int id) { return g_mgr.find(g, id); }

int TrackLeft()  { const auto* c = C("damp-row", ID_DAMP_TRACK); return c ? c->rect.left  : kPad + kLabelW; }
int TrackRight() { const auto* c = C("damp-row", ID_DAMP_TRACK); return c ? c->rect.right : kPad + kLabelW + kTrackW; }
int DampCenterY(){ const auto* c = C("damp-row", ID_DAMP_TRACK); return c ? (c->rect.top + c->rect.bottom) / 2 : 0; }
int SensCenterY(){ const auto* c = C("sens-row", ID_SENS_TRACK); return c ? (c->rect.top + c->rect.bottom) / 2 : 0; }

// 鼠标 x → 阻尼值（0~100），实时写 App.camera.damping + 保存（事件驱动，不再经全局轮询）
void UpdateDampingFromX(int x) {
    const int left = TrackLeft(), right = TrackRight();
    float t = (right > left) ? static_cast<float>(x - left) / static_cast<float>(right - left) : 0.0f;
    int v = static_cast<int>(t * 100.0f + 0.5f);
    v = std::max(0, std::min(100, v));
    if (g_settingsApp) {
        g_settingsApp->camera.damping = v / 100.0f;
        SaveSettingInt("camera_damping", v);
    }
}

// 鼠标 x → 灵敏度值（0~100），实时写 App.camera.orbitSensitivity + 保存（事件驱动，不再经全局轮询）
void UpdateSensitivityFromX(int x) {
    const int left = TrackLeft(), right = TrackRight();
    float t = (right > left) ? static_cast<float>(x - left) / static_cast<float>(right - left) : 0.0f;
    int v = static_cast<int>(t * 100.0f + 0.5f);
    v = std::max(0, std::min(100, v));
    if (g_settingsApp) {
        g_settingsApp->camera.orbitSensitivity = SensToFactor(v);
        SaveSettingInt("mouse_sensitivity", v);
    }
}

RECT GetHeaderRect() {
    const auto* c = C("settings", ID_HEADER);
    return c ? c->rect : RECT{kPad, kPad, kPad + kContainerW, kPad + kItemH};
}

// 列表**整体容器**（用户 172 轮：有容器背景；固定显示 kMaxVisible 行，内容可增长）
RECT GetListContainerRect() {
    const auto* c = C("settings", ID_LIST);
    if (c) return c->rect;
    const int top = kPad + kItemH + kItemGap;
    return { kPad, top, kPad + kContainerW, top + kMaxVisible * (kItemH + kItemGap) };
}

// 可见行矩形（visibleRow = 0..kMaxVisible-1，对应内容索引 g_scrollOffset + visibleRow）
RECT GetOptionRect(int visibleRow) {
    const RECT c = GetListContainerRect();
    const int y = c.top + 1 + visibleRow * (kItemH + kItemGap);
    // 内容超出可见数时右侧让出滚动条宽度
    const int right = (kAACount > kMaxVisible) ? c.right - kScrollbarW - 4 : c.right - 1;
    return { c.left + 1, y, right, y + kItemH };
}

// 滚动条轨道（右侧）
RECT GetScrollTrackRect() {
    const RECT c = GetListContainerRect();
    return { c.right - kScrollbarW - 2, c.top + 2, c.right - 2, c.bottom - 2 };
}

// 滚动条滑块（按 scrollOffset 比例）
RECT GetScrollThumbRect() {
    const RECT t = GetScrollTrackRect();
    const int trackH = t.bottom - t.top;
    const int thumbH = std::max(trackH * kMaxVisible / kAACount, 16);
    const int maxOffset = kAACount - kMaxVisible;
    const int thumbY = t.top + (maxOffset > 0 ? g_scrollOffset * (trackH - thumbH) / maxOffset : 0);
    return { t.left, thumbY, t.right, thumbY + thumbH };
}

// 滚动偏移 clamp 到合法范围
void ClampScroll() {
    const int maxOffset = std::max(kAACount - kMaxVisible, 0);
    g_scrollOffset = std::max(0, std::min(maxOffset, g_scrollOffset));
}

// 列表动画每帧推进（WM_TIMER 驱动，16ms 一帧；到终点自动停表）
void UpdateListAnim() {
    if (g_settingsWindow == nullptr) return;
    const float t = static_cast<float>(GetTickCount64() - g_listAnimStartMs) / 1000.0f;
    const float dur = g_listAnimOpen ? kListExpandSec : kListCollapseSec;
    const float k = (dur > 0.0f) ? std::min(t / dur, 1.0f) : 1.0f;
    g_listAnim = g_listAnimFrom + (g_listAnimOpen ? (1.0f - g_listAnimFrom) : -g_listAnimFrom) * k;
    if (k >= 1.0f) {
        g_listAnim = g_listAnimOpen ? 1.0f : 0.0f;
        KillTimer(g_settingsWindow, kAnimTimerId);
    }
}

// 展开/收起切换（与主窗口菜单 menuAnim 同款动画）
void ToggleList() {
    g_listOpen = !g_listOpen;
    g_listAnimOpen = g_listOpen;
    g_listAnimFrom = g_listAnim;
    g_listAnimStartMs = GetTickCount64();
    if (g_listOpen) g_scrollOffset = 0;  // 每次展开从最顶开始
    SetTimer(g_settingsWindow, kAnimTimerId, 16, nullptr);
    InvalidateRect(g_settingsWindow, nullptr, FALSE);
}

// 收起列表（选择项后调用，带动画）
void CollapseList() {
    g_listOpen = false;
    g_listAnimOpen = false;
    g_listAnimFrom = g_listAnim;
    g_listAnimStartMs = GetTickCount64();
    SetTimer(g_settingsWindow, kAnimTimerId, 16, nullptr);
    InvalidateRect(g_settingsWindow, nullptr, FALSE);
}

// 从 exe 内部资源加载图标（替代程序生成的齿轮图标）
HICON CreateGearIcon() {
    static HICON cached = nullptr;
    if (!cached) {
        cached = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    }
    return cached;
}

void DrawRow(HDC hdc, const RECT& r, COLORREF fill, int borderPx, HPEN basePen, HBRUSH baseBrush) {
    HPEN itemPen = CreatePen(PS_SOLID, borderPx, kBorderDim);
    HBRUSH itemBrush = CreateSolidBrush(fill);
    SelectObject(hdc, itemPen);
    SelectObject(hdc, itemBrush);
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, basePen);
    SelectObject(hdc, baseBrush);
    DeleteObject(itemPen);
    DeleteObject(itemBrush);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        if (!g_font) {
            g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
        }
        // 缓存 GDI 对象（一次创建，重绘复用）
        g_borderPen   = CreatePen(PS_SOLID, 1, kBorderDim);
        g_btnBrush    = CreateSolidBrush(kButtonColor);
        g_arrowPen    = CreatePen(PS_SOLID, 1, kTextSel);
        g_arrowBrush  = CreateSolidBrush(kTextSel);
        g_trackPen    = CreatePen(PS_SOLID, 1, kTrackColor);
        g_trackBrush  = CreateSolidBrush(kTrackColor);
        g_fillBrush   = CreateSolidBrush(kTrackFill);
        g_ringBrush   = CreateSolidBrush(kThumbRing);
        g_whiteBrush  = CreateSolidBrush(kThumbWhite);
        g_listBg      = CreateSolidBrush(kListBgColor);
        g_scrollTrackBrush = CreateSolidBrush(kScrollTrack);
        g_scrollThumbBrush = CreateSolidBrush(kScrollThumb);
        g_bgBrush     = CreateSolidBrush(kDefaultPanelRgb);
        g_smallFont   = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
        return 0;
    case WM_PAINT: {
        RecomputeSettingsLayout(hwnd);  // 顺位布局：绘制前按当前窗口尺寸重算所有控件 rect
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT full;
        GetClientRect(hwnd, &full);
        const int cw = full.right - full.left;
        const int ch = full.bottom - full.top;
        // 双缓冲（用户 169 轮：拖动滑块防闪烁）：全部绘制先到内存 DC，
        // 完成后 BitBlt 一次性上屏——避免 GDI 逐个图元绘制造成的中间态闪烁。
        // 内存 DC/位图惰性缓存（尺寸变化时重建），避免每次重绘反复 CreateCompatibleDC/Bitmap。
        if (!g_db.dc || g_db.w != cw || g_db.h != ch) {
            ui2d::FreeDoubleBuffer(g_db);
            g_db = ui2d::CreateDoubleBuffer(hdc, cw, ch);
        }
        HDC memDc = g_db.dc;

        FillRect(memDc, &full, g_bgBrush);

        HGDIOBJ oldFont = SelectObject(memDc, g_font);
        SetBkMode(memDc, TRANSPARENT);

        HGDIOBJ oldPen = SelectObject(memDc, g_borderPen);
        HGDIOBJ oldBrush = SelectObject(memDc, g_btnBrush);

        const RECT hdr = GetHeaderRect();
        // Round372：header 按 hover/active 着色（与主窗口按钮管线对齐：normal/hover/active 边框）
        COLORREF hdrBg = kButtonColor;
        if (g_hoverButton == 0) {
            // hover 提亮（×1.5 + 0.08，RGB 三通道封顶 255；与主窗口 Round363 一致）
            int r = std::min(255, static_cast<int>(GetRValue(kButtonColor) * 1.5f + 0.08f * 255));
            int g = std::min(255, static_cast<int>(GetGValue(kButtonColor) * 1.5f + 0.08f * 255));
            int b = std::min(255, static_cast<int>(GetBValue(kButtonColor) * 1.5f + 0.08f * 255));
            hdrBg = RGB(r, g, b);
        }
        HBRUSH hdrBrush = CreateSolidBrush(hdrBg);
        HBRUSH hdrOldBrush = static_cast<HBRUSH>(SelectObject(memDc, hdrBrush));
        HPEN   hdrOldPen   = static_cast<HPEN>(SelectObject(memDc, g_borderPen));
        RoundRect(memDc, hdr.left, hdr.top, hdr.right, hdr.bottom, kCornerD, kCornerD);
        // 下拉 header：仅鼠标触碰(hover)才 1px 黄边；未触碰列表（含收起态）不黄亮
        if (g_hoverButton == 0) {
            const HPEN yb = CreatePen(PS_SOLID, 1, kYellowBorder);
            HPEN ybOld = static_cast<HPEN>(SelectObject(memDc, yb));
            HBRUSH nb = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HBRUSH nbOld = static_cast<HBRUSH>(SelectObject(memDc, nb));
            RoundRect(memDc, hdr.left, hdr.top, hdr.right, hdr.bottom, kCornerD, kCornerD);
            SelectObject(memDc, ybOld);
            SelectObject(memDc, nbOld);
            DeleteObject(yb);
            SelectObject(memDc, hdrBrush);
        }
        SelectObject(memDc, hdrOldBrush);
        SelectObject(memDc, hdrOldPen);
        DeleteObject(hdrBrush);
        RECT textRect = hdr;
        textRect.left += kTextPad;
        SetTextColor(memDc, kTextSel);
        DrawTextW(memDc, kAANames[CurAa()], -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // 恢复后续代码使用的 brush/pen（原顺序：borderPen+btnBrush）
        SelectObject(memDc, g_borderPen);
        SelectObject(memDc, g_btnBrush);
        {
            const int acx = hdr.right - 22;
            const int acy = (hdr.top + hdr.bottom) / 2;
            POINT tri[3];
            if (g_listOpen) {
                tri[0] = { acx - 5, acy + 3 };
                tri[1] = { acx + 5, acy + 3 };
                tri[2] = { acx, acy - 4 };
            } else {
                tri[0] = { acx - 5, acy - 3 };
                tri[1] = { acx + 5, acy - 3 };
                tri[2] = { acx, acy + 4 };
            }
            SelectObject(memDc, g_arrowPen);
            SelectObject(memDc, g_arrowBrush);
            Polygon(memDc, tri, 3);
            SelectObject(memDc, g_borderPen);
            SelectObject(memDc, g_btnBrush);
        }

        // ---- 摄像机阻尼滑动条（用户 168 轮："自定义现代化滑动条"）----
        {
            const int cy = DampCenterY();
            const int left = TrackLeft(), right = TrackRight();
            const int trackTop = cy - kTrackH / 2;
            // 左侧标签：摄像机当前阻尼： **
            wchar_t label[64];
            wsprintfW(label, L"摄像机当前阻尼： %d", CurDamp());
            SetTextColor(memDc, kTextSel);
            const auto* dl = C("damp-row", ID_DAMP_LABEL);
            RECT lr = dl ? dl->rect : RECT{kPad, 0, kPad + kLabelW, 0};
            DrawTextW(memDc, label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            // 轨道背景（圆角，深灰）
            SelectObject(memDc, g_trackPen);
            SelectObject(memDc, g_trackBrush);
            RoundRect(memDc, left, trackTop, right, trackTop + kTrackH, 3, 3);
            // 已填充段（轨道左端 → 滑块中心）
            const int thumbX = left + static_cast<int>(CurDamp() / 100.0f * kTrackW + 0.5f);
            SelectObject(memDc, g_fillBrush);
            RoundRect(memDc, left, trackTop, thumbX, trackTop + kTrackH, 3, 3);
            // 圆形滑块：外圈浅灰（"一丝透明边"）+ 内圈纯白
            SelectObject(memDc, g_ringBrush);
            SelectObject(memDc, GetStockObject(NULL_PEN));
            Ellipse(memDc, thumbX - kThumbR - 1, cy - kThumbR - 1,
                    thumbX + kThumbR + 1, cy + kThumbR + 1);
            SelectObject(memDc, g_whiteBrush);
            Ellipse(memDc, thumbX - kThumbR + 1, cy - kThumbR + 1,
                    thumbX + kThumbR - 1, cy + kThumbR - 1);
            SelectObject(memDc, g_borderPen);
            SelectObject(memDc, g_btnBrush);
        }

        // ---- 鼠标滑动灵敏度滑动条（用户 2026-08-20：在摄像机阻尼下方）----
        {
            const int cy = SensCenterY();
            const int left = TrackLeft(), right = TrackRight();
            const int trackTop = cy - kTrackH / 2;
            // 左侧标签：鼠标滑动灵敏度： **
            wchar_t label[64];
            wsprintfW(label, L"鼠标滑动灵敏度： %d", CurSens());
            SetTextColor(memDc, kTextSel);
            const auto* sl = C("sens-row", ID_SENS_LABEL);
            RECT lr = sl ? sl->rect : RECT{kPad, 0, kPad + kLabelW, 0};
            DrawTextW(memDc, label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            // 轨道背景（圆角，深灰）
            SelectObject(memDc, g_trackPen);
            SelectObject(memDc, g_trackBrush);
            RoundRect(memDc, left, trackTop, right, trackTop + kTrackH, 3, 3);
            // 已填充段（轨道左端 → 滑块中心）
            const int thumbX = left + static_cast<int>(CurSens() / 100.0f * kTrackW + 0.5f);
            SelectObject(memDc, g_fillBrush);
            RoundRect(memDc, left, trackTop, thumbX, trackTop + kTrackH, 3, 3);
            // 圆形滑块：外圈浅灰（"一丝透明边"）+ 内圈纯白
            SelectObject(memDc, g_ringBrush);
            SelectObject(memDc, GetStockObject(NULL_PEN));
            Ellipse(memDc, thumbX - kThumbR - 1, cy - kThumbR - 1,
                    thumbX + kThumbR + 1, cy + kThumbR + 1);
            SelectObject(memDc, g_whiteBrush);
            Ellipse(memDc, thumbX - kThumbR + 1, cy - kThumbR + 1,
                    thumbX + kThumbR - 1, cy + kThumbR - 1);
            SelectObject(memDc, g_borderPen);
            SelectObject(memDc, g_btnBrush);
        }

        // ---- 抗锯齿下拉列表（用户 172 轮：整体容器背景 + 内容可滚动；
        //       174 轮：展开/收起动画——容器高度按 g_listAnim 从 0 长到满，内容从顶部逐行露出）----
        if (g_listOpen || g_listAnim > 0.001f) {
            const RECT c = GetListContainerRect();
            const int curH = c.bottom - c.top;  // 容器 rect 已含展开动画高度，直接用它裁剪
            if (curH >= 1) {
                const int savedDc = SaveDC(memDc);
                // 裁剪到动画高度（内容被裁剪，产生"从 header 向下展开"效果）
                RECT clip = c;
                clip.bottom = c.top + curH;
                IntersectClipRect(memDc, clip.left, clip.top, clip.right, clip.bottom);
                // 1) 整体容器背景板（列表随内容增长，容器固定显示 kMaxVisible 行）
                SelectObject(memDc, g_borderPen);
                SelectObject(memDc, g_listBg);
                RoundRect(memDc, c.left, c.top, c.right, c.bottom,
                          ui::g_theme.dropdown.corner, ui::g_theme.dropdown.corner);
                // 2) 可见行（最多 kMaxVisible 行，从 scrollOffset 开始）
                const int visible = std::min(kMaxVisible, kAACount - g_scrollOffset);
                for (int row = 0; row < visible; ++row) {
                    const int opt = g_scrollOffset + row;
                    const RECT r = GetOptionRect(row);
                    const bool selected = (opt == CurAa());
                    const bool hovered  = (g_hoverButton == row + 1);  // 本行被鼠标触碰
                    if (selected) {
                        // 选中：与未选中行同底色(kRowColor) + 1px 白边（白色高亮；方框与行严格对齐，消除偏移/浮空感）
                        const HPEN   hPen    = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        const HPEN   hPenOld = static_cast<HPEN>(SelectObject(memDc, hPen));
                        const HBRUSH hBr     = CreateSolidBrush(kRowColor);
                        const HBRUSH hBrOld  = static_cast<HBRUSH>(SelectObject(memDc, hBr));
                        Rectangle(memDc, r.left, r.top, r.right, r.bottom);
                        SelectObject(memDc, hPenOld);
                        SelectObject(memDc, hBrOld);
                        DeleteObject(hPen);
                        DeleteObject(hBr);
                    } else if (hovered) {
                        // 未选中但被触碰：1px 白边 + 加深底色（kHoverRowColor，弱于选中态）
                        const HPEN   hPen    = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        const HPEN   hPenOld = static_cast<HPEN>(SelectObject(memDc, hPen));
                        const HBRUSH hBr     = CreateSolidBrush(kHoverRowColor);
                        const HBRUSH hBrOld  = static_cast<HBRUSH>(SelectObject(memDc, hBr));
                        RoundRect(memDc, r.left, r.top, r.right, r.bottom, 2, 2);
                        SelectObject(memDc, hPenOld);
                        SelectObject(memDc, hBrOld);
                        DeleteObject(hPen);
                        DeleteObject(hBr);
                    } else {
                        DrawRow(memDc, r, kRowColor, 1, g_borderPen, g_btnBrush);
                    }
                    RECT tr = r;
                    tr.left += kTextPad;
                    // 其余未选中选项文字改为白色；选中项保持主题正文色
                    SetTextColor(memDc, selected ? kTextSel : RGB(255, 255, 255));
                    DrawTextW(memDc, kAANames[opt], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                // 3) 滚动条（内容 > 可见数时出现）
                if (kAACount > kMaxVisible) {
                    const RECT t = GetScrollTrackRect();
                    SelectObject(memDc, g_scrollTrackBrush);
                    SelectObject(memDc, GetStockObject(NULL_PEN));
                    RoundRect(memDc, t.left, t.top, t.right, t.bottom, 4, 4);
                    const RECT th = GetScrollThumbRect();
                    SelectObject(memDc, g_scrollThumbBrush);
                    RoundRect(memDc, th.left, th.top, th.right, th.bottom, 3, 3);
                }
                RestoreDC(memDc, savedDc);
                // 恢复绘制状态（后续底部文字等）
                SelectObject(memDc, g_borderPen);
                SelectObject(memDc, g_btnBrush);
            }
        }

        if (g_renderBottomText[0]) {
            RECT cr;
            GetClientRect(hwnd, &cr);
            HGDIOBJ oldSmallFont = SelectObject(memDc, g_smallFont);
            SetTextColor(memDc, ui::g_theme.palette.textDim);
            RECT br = cr;
            br.left += 2;
            br.top = cr.bottom - 24;
            br.bottom = cr.bottom - 2;
            DrawTextW(memDc, g_renderBottomText, -1, &br,
                      DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
            // 右下角：程序图标署名（用户 2026-08-19 要求）
            RECT br2 = cr;
            br2.right -= 4;
            br2.top = cr.bottom - 24;
            br2.bottom = cr.bottom - 2;
            DrawTextW(memDc, L"本软件程序图标由：幻影_冰川下车 提供", -1, &br2,
                      DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
            SelectObject(memDc, oldSmallFont);
        }

        SelectObject(memDc, oldPen);
        SelectObject(memDc, oldBrush);
        SelectObject(memDc, oldFont);

        // 一次性上屏（双缓冲 DC 已缓存，无需释放）
        BitBlt(hdc, 0, 0, cw, ch, memDc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RecomputeSettingsLayout(hwnd);  // 顺位布局：点击命中测试前重算 rect
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        const POINT pt{ x, y };
        const RECT hdr = GetHeaderRect();
        // 抗锯齿下拉 header（切换展开/收起，带动画）
        if (PtInRect(&hdr, pt)) {
            ToggleList();
            return 0;
        }
        // 列表展开时：列表项/滚动条**优先**响应（覆盖在滑条上方，用户 170 轮"优先级最高"）
        if (g_listOpen) {
            // 滚动条 thumb：开始拖动
            if (kAACount > kMaxVisible) {
                const RECT th = GetScrollThumbRect();
                if (PtInRect(&th, pt)) {
                    g_scrollDragging = true;
                    g_scrollDragStartY = y;
                    g_scrollDragStartOffset = g_scrollOffset;
                    SetCapture(hwnd);
                    return 0;
                }
                // 滚动条轨道：点击跳转
                const RECT t = GetScrollTrackRect();
                if (PtInRect(&t, pt)) {
                    const int trackH = t.bottom - t.top;
                    const int thumbH = std::max(trackH * kMaxVisible / kAACount, 16);
                    const int maxOffset = kAACount - kMaxVisible;
                    g_scrollOffset = (trackH > thumbH)
                        ? (y - t.top - thumbH / 2) * maxOffset / (trackH - thumbH)
                        : 0;
                    ClampScroll();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            // 列表项：opt = scrollOffset + 可见行
            for (int row = 0; row < kMaxVisible; ++row) {
                const int opt = g_scrollOffset + row;
                if (opt >= kAACount) break;
                const RECT r = GetOptionRect(row);
                if (PtInRect(&r, pt)) {
                    if (g_settingsApp) {
                        const bool ok = ApplyAAMode(*g_settingsApp, static_cast<AAMode>(opt));
                        if (!ok) VkbLog("[aa] 抗锯齿切换失败，保持当前模式");
                    }
                    CollapseList();  // 选择后收起（带动画）
                    return 0;
                }
            }
        }
        // 摄像机阻尼滑块（列表收起时才响应，避免与展开列表重叠冲突）
        {
            const int cy = DampCenterY();
            RECT hit{ TrackLeft() - kThumbR, cy - kThumbR - 4,
                      TrackRight() + kThumbR, cy + kThumbR + 4 };
            if (PtInRect(&hit, pt)) {
                UpdateDampingFromX(x);
                g_dragging = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        // 鼠标滑动灵敏度滑块（列表收起时才响应，避免与展开列表重叠冲突）
        {
            const int cy = SensCenterY();
            RECT hit{ TrackLeft() - kThumbR, cy - kThumbR - 4,
                      TrackRight() + kThumbR, cy + kThumbR + 4 };
            if (PtInRect(&hit, pt)) {
                UpdateSensitivityFromX(x);
                g_draggingSens = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        // 列表展开时点击别处（非 header/列表项/滚动条/滑块）→ 收起列表
        if (g_listOpen) CollapseList();
        return 0;
    }
    // 不擦背景（用户 170 轮"滑条滑动还会闪烁"）：双缓冲已覆盖全客户区，
    // 系统擦背景（hbrBackground）会产生黑闪——直接返回 1 跳过擦除，闪烁彻底消除
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        RecomputeSettingsLayout(hwnd);  // 顺位布局：hover 命中测试前重算 rect
        if (g_scrollDragging) {
            // 滚动条 thumb 拖动：按鼠标位移换算滚动偏移
            const RECT t = GetScrollTrackRect();
            const int trackH = t.bottom - t.top;
            const int thumbH = std::max(trackH * kMaxVisible / kAACount, 16);
            const int maxOffset = kAACount - kMaxVisible;
            const int y = static_cast<short>(HIWORD(lParam));
            const int dy = y - g_scrollDragStartY;
            if (trackH > thumbH) {
                g_scrollOffset = g_scrollDragStartOffset + dy * maxOffset / (trackH - thumbH);
                ClampScroll();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (g_dragging) {
            const int x = static_cast<short>(LOWORD(lParam));
            UpdateDampingFromX(x);
            InvalidateRect(hwnd, nullptr, FALSE);  // FALSE=不擦背景，全量重绘交给 WM_PAINT 双缓冲
        }
        if (g_draggingSens) {
            const int x = static_cast<short>(LOWORD(lParam));
            UpdateSensitivityFromX(x);
            InvalidateRect(hwnd, nullptr, FALSE);  // FALSE=不擦背景，全量重绘交给 WM_PAINT 双缓冲
        }
        // Round372：按钮 hover 检测——header + 列表可见行；变化时 InvalidateRect 触发重绘
        const POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        int newHover = -1;
        const RECT hdr = GetHeaderRect();
        if (PtInRect(&hdr, pt)) newHover = 0;
        else if (g_listOpen) {
            for (int row = 0; row < kMaxVisible; ++row) {
                const int opt = g_scrollOffset + row;
                if (opt >= kAACount) break;
                const RECT r = GetOptionRect(row);
                if (PtInRect(&r, pt)) { newHover = row + 1; break; }
            }
        }
        if (newHover != g_hoverButton) {
            g_hoverButton = newHover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        // 首次收到 WM_MOUSEMOVE 时请求离开通知——鼠标移出窗口重置 hover
        if (!g_mouseTracked) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            g_mouseTracked = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:   // Round372：鼠标离开窗口——重置 hover
        g_mouseTracked = false;
        if (g_hoverButton != -1) {
            g_hoverButton = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kAnimTimerId) {
            UpdateListAnim();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        // 列表展开时滚轮滚动（内容 > 可见数才有效）
        if (g_listOpen && kAACount > kMaxVisible) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_scrollOffset -= (delta > 0) ? 1 : -1;  // 上滚 → 看前面
            ClampScroll();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_scrollDragging) {
            g_scrollDragging = false;
            ReleaseCapture();
        }
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
        }
        if (g_draggingSens) {
            g_draggingSens = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_dragging = false;
        g_draggingSens = false;
        g_scrollDragging = false;
        return 0;
    case WM_CLOSE:
        // 焦点转移失败 → 主窗口失去前台、被其他程序覆盖（看起来像"隐藏"）
        if (g_owner && IsWindow(g_owner)) {
            EnableWindow(g_owner, TRUE);
            SetForegroundWindow(g_owner);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kAnimTimerId);
        // 释放缓存的 GDI 对象与双缓冲 DC
        ui2d::FreeDoubleBuffer(g_db);
        DeleteObject(g_borderPen);          g_borderPen = nullptr;
        DeleteObject(g_btnBrush);           g_btnBrush = nullptr;
        DeleteObject(g_arrowPen);           g_arrowPen = nullptr;
        DeleteObject(g_arrowBrush);         g_arrowBrush = nullptr;
        DeleteObject(g_trackPen);           g_trackPen = nullptr;
        DeleteObject(g_trackBrush);         g_trackBrush = nullptr;
        DeleteObject(g_fillBrush);          g_fillBrush = nullptr;
        DeleteObject(g_ringBrush);          g_ringBrush = nullptr;
        DeleteObject(g_whiteBrush);         g_whiteBrush = nullptr;
        DeleteObject(g_listBg);             g_listBg = nullptr;
        DeleteObject(g_scrollTrackBrush);   g_scrollTrackBrush = nullptr;
        DeleteObject(g_scrollThumbBrush);   g_scrollThumbBrush = nullptr;
        DeleteObject(g_smallFont);          g_smallFont = nullptr;
        DeleteObject(g_bgBrush);            g_bgBrush = nullptr;
        g_settingsWindow = nullptr;
        g_owner = nullptr;
        g_listOpen = false;
        g_dragging = false;
        g_draggingSens = false;
        g_scrollOffset = 0;
        g_scrollDragging = false;
        g_listAnim = 0.0f;
        g_listAnimOpen = false;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}

wchar_t g_renderBottomText[160] = L"";

HWND OpenSettingsWindow(HWND owner, App& app) {
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        SetForegroundWindow(g_settingsWindow);
        return g_settingsWindow;
    }
    g_settingsApp = &app;
    g_listOpen = false;
    g_dragging = false;
    g_draggingSens = false;
    g_scrollOffset = 0;
    g_scrollDragging = false;
    g_listAnim = 0.0f;
    g_listAnimFrom = 0.0f;
    g_listAnimOpen = false;
    static bool registered = false;
    if (!registered) {
        // 复用共享 GDI 窗口类注册（消除逐字复制的 WNDCLASSEXW 脚手架）
        ui2d::RegisterWindowClass(kSettingsClassName, SettingsWndProc, kDefaultPanelRgb,
                                 CreateGearIcon(), CreateGearIcon());
        registered = true;
    }
    // 创建可缩放窗口，默认 1100×700；去掉 WS_MAXIMIZEBOX → 不可最大化（禁用最大化按钮）
    HWND wnd = CreateWindowExW(0, kSettingsClassName, kSettingsTitle,
                               WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!wnd) return nullptr;
    g_settingsWindow = wnd;
    g_owner = owner;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
    return wnd;
}

void CloseSettingsWindow() {
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        DestroyWindow(g_settingsWindow);
        g_settingsWindow = nullptr;
    }
    g_settingsApp = nullptr;
}

// ============================================================================
// ui_presets.h — awa 统一控件预设体系
// 用途：所有窗口（主界面 2D 面板 / 导入窗口 / 设置窗口 / MC 窗口）的控件
//       尺寸、间距、颜色、圆角一律从预设取值；以后新建控件 =
//       引用 ui::g_theme 对应预设 + 调节个别参数，禁止再硬编码。
// 用法示例：
//   ui::ButtonPreset btn = ui::g_theme.button;      // 复制预设
//   btn.normal = RGB(40,80,160);                     // 调节个别参数
//   DrawGdiButton(dc, b, btn, L"确定", ...);        // 绘制
// ============================================================================
#pragma once
#include <windows.h>

namespace ui {

// ---- 调色板 ----
struct Palette {
    COLORREF bg          = RGB(48, 48, 48);    // 窗口背景
    COLORREF panelBg     = RGB(64, 64, 68);    // 面板背景
    COLORREF panelBorder = RGB(120, 130, 150); // 面板描边
    COLORREF text        = RGB(255, 255, 255); // 正文
    COLORREF textDim     = RGB(160, 160, 160); // 次要文字
    COLORREF accent      = RGB(70, 140, 220);  // 主色/选中
    COLORREF accentHover = RGB(90, 165, 245);  // 悬停
    COLORREF accentPress = RGB(50, 110, 190);  // 按下
    COLORREF inputBg     = RGB(38, 38, 42);    // 列表/输入背景
    COLORREF scrollBar   = RGB(90, 90, 100);   // 滚动条
};

// ---- 空间预设 ----
struct Spacing {
    int gap      = 8;    // 控件间距
    int padX     = 12;   // 内边距
    int padY     = 6;
    int corner   = 6;    // 圆角半径
    int btnW     = 120;  // 按钮默认宽
    int btnH     = 32;   // 按钮默认高
    int rowH     = 26;   // 列表行高
    int panelPad = 14;   // 面板内边距
};

// ---- 按钮预设（字段与原 ui_button.h 的 ButtonTheme 兼容）----
struct ButtonPreset {
    COLORREF normal      = RGB(60, 60, 64);
    COLORREF pressed     = RGB(45, 45, 50);
    COLORREF released    = RGB(60, 60, 64);
    COLORREF hoverBorder = RGB(140, 180, 240);
    COLORREF textColor   = RGB(255, 255, 255);
    float    animSpeed   = 0.18f;
    int      corner      = 6;
    int      textPadLeft = 10;
};

// ---- 列表预设 ----
struct ListPreset {
    COLORREF bg       = RGB(38, 38, 42);
    COLORREF hover    = RGB(55, 55, 62);
    COLORREF selected = RGB(70, 140, 220);
    COLORREF text     = RGB(255, 255, 255);
    int rowH          = 26;
    int corner        = 4;
};

// ---- 滑条预设 ----
struct SliderPreset {
    int      w = 200, h = 8, knobR = 9;
    COLORREF track      = RGB(50, 50, 55);
    COLORREF fill       = RGB(70, 140, 220);
    COLORREF knob       = RGB(255, 255, 255);
    COLORREF knobBorder = RGB(140, 180, 240);
};

// ---- 下拉预设 ----
struct DropdownPreset {
    COLORREF bg     = RGB(38, 38, 42);
    COLORREF border = RGB(90, 90, 100);
    COLORREF text   = RGB(255, 255, 255);
    COLORREF hover  = RGB(55, 55, 62);
    int corner      = 4;
    int itemH       = 26;
};

// ---- 面板预设 ----
struct PanelPreset {
    COLORREF bg     = RGB(64, 64, 68);
    COLORREF border = RGB(120, 130, 150);
    int corner      = 6;
};

// ---- Vulkan 用 float 颜色（0~1）----
struct FloatColor { float r = 0.f, g = 0.f, b = 0.f, a = 1.f; };
inline FloatColor ToFloat(COLORREF c, float a = 1.0f) {
    return { GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a };
}

// ---- 全局主题（C++17 inline 变量：无需单独 .cpp，不改 CMakeLists）----
struct Theme {
    Palette        palette;
    Spacing        spacing;
    ButtonPreset   button;
    ListPreset     list;
    SliderPreset   slider;
    DropdownPreset dropdown;
    PanelPreset    panel;
};
inline Theme g_theme;   // 全局默认主题；各窗口初始化时可整体或个别覆盖

} // namespace ui

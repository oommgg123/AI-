// ============================================================================
//   共享 GDI 按钮渲染管线（用户 183+ 轮：以后每一个按钮都必须走这套）
//   与主窗口 Vulkan 按钮完全一致：
//     - 状态机 ButtonStateMachine（Normal→Hover→Pressed→Released）
//     - 主题 ButtonTheme（normal/pressed/released/hover_border + anim_speed）
//     - 动画 UpdateButtonColor（每帧 lerp 到当前状态目标色）
//     - 绘制 DrawGdiButton（圆角矩形 + 主题填充 + hover 高亮描边 + 文字）
//   完整流程：按下(Pressed) → 释放(Released) → 触发 onClick → 回到 Normal/Hover，
//   全程带平滑颜色/描边动画。GDI 窗口（导入窗口、设置窗口）统一复用，不再手绘裸矩形。
// ============================================================================
#pragma once

#include "app.h"   // UiButton / ButtonTheme / ButtonState（app.h 已包含 <windows.h>）

// 每帧推进按钮颜色/边框动画（lerp 到当前状态目标色）。
// 应在 WM_TIMER 或渲染循环中调用（动画期间持续调用直至收敛）。
void UpdateButtonColor(UiButton& b, const ButtonTheme& theme);

// 命中测试：屏幕坐标(x,y)是否落在按钮矩形内（x/y 为浮点像素坐标）。
bool PointInButton(const UiButton& b, float x, float y);

// 用 GDI 绘制按钮：圆角矩形 + 主题填充色(b.color) + hover 高亮描边(b.border) + 文字。
// label 为 nullptr 时不画文字；textColor 为文字颜色；textPadLeft 为文字相对按钮左边的内边距。
// borderOnly=true：跳过填充，仅用 1px 笔沿 b.border 色画圆角/圆描边（供"白圈"等纯边框 UI 使用）。
void DrawGdiButton(HDC dc, const UiButton& b, const ButtonTheme& theme,
                   const wchar_t* label, int textPadLeft, HFONT labelFont, COLORREF textColor,
                   bool borderOnly = false);

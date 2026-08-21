// ============================================================================
//
// 独立的设置子页面：可缩放、背景"默认色"、齿轮图标；模态——打开期间主窗口不可交互。
// ============================================================================
#pragma once

#include <windows.h>

enum class AAMode : int { None = 0, MSAA_2x = 1, MSAA_4x = 2, FXAA = 3, SSAA = 4 };

extern int g_selectedAaMode;

// 摄像机阻尼（0~100 整数百分比）：设置窗口滑动条直接写这个全局变量，
// 主循环每帧检测差异 → 应用到 camera.damping 并保存 awa_settings.txt（与 aa_mode 同模式）
extern int g_cameraDamping;

// 鼠标滑动灵敏度（0~100 整数）：设置窗口滑动条直接写这个全局变量，
// 主循环每帧检测差异 → 映射到 camera.orbitSensitivity（旋转/平移输入增益）并保存
extern int g_mouseSensitivity;

extern wchar_t g_renderBottomText[160];

// 重复调用则聚焦已打开的窗口（不重复创建）。返回子窗口句柄（失败返回 nullptr）。
HWND OpenSettingsWindow(HWND owner, int currentAaMode);

void CloseSettingsWindow();

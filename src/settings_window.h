// ============================================================================
//
// 独立的设置子页面：可缩放、背景"默认色"、齿轮图标；模态——打开期间主窗口不可交互。
// ============================================================================
#pragma once

#include <windows.h>

enum class AAMode : int { None = 0, MSAA_2x = 1, MSAA_4x = 2, FXAA = 3, SSAA = 4 };

struct App;  // 前向声明：OpenSettingsWindow 仅持 App&，无需完整定义（避免与 app.h 的循环包含）

extern wchar_t g_renderBottomText[160];

// 重复调用则聚焦已打开的窗口（不重复创建）。返回子窗口句柄（失败返回 nullptr）。
// 直接持有 App&：滑动条/选项变更时即时写回 app（camera.damping / orbitSensitivity / aa.aaMode）+ 保存，
// 不再经外部全局 + 主循环每帧轮询。
HWND OpenSettingsWindow(HWND owner, App& app);

void CloseSettingsWindow();

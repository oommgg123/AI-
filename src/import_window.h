// ============================================================================
//   导入窗口（用户 180 轮：点击导入按钮先弹出 500×700 默认色窗口）
//   GDI 自绘 + 双缓冲（与设置窗口同风格）；模态
//   用户 181 轮修复：点击"导入 3D 模型…"→ 以导入窗口为 owner 弹文件对话框（窗口保持），
//   选好文件才关窗；所有关闭路径统一在 WM_DESTROY 恢复主窗口（不再永久模态）
// ============================================================================
#pragma once

#include <windows.h>

#include <string>

// 导入窗口里点击"导入 3D 模型…"且**文件选择成功**后置 true，并写入 g_importPath；
// 主循环检测后走 LaunchImport 流程
extern bool g_importWindowConfirm;
extern std::wstring g_importPath;

// 导入窗口里点击"我的世界地图导入(pe)"且**目录选择成功**后置 true，并写入 g_mcWorldPath；
// 主循环检测后走 MC 世界导入流程（目前功能开发中，仅记录路径）
extern bool g_mcWorldConfirm;
extern std::wstring g_mcWorldPath;

// 打开导入窗口（重复调用聚焦已有窗口）。返回窗口句柄（失败 nullptr）
HWND OpenImportWindow(HWND owner);

void CloseImportWindow();

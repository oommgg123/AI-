// ============================================================================
//
// 现代化与兼容性：
// * 兼容路径：传统 render pass + framebuffer（Vulkan 1.0 core）
// —— 运行时探测设备能力，老显卡 / 核显不支持动态渲染时自动回退
// - 实例 apiVersion 申请 1.0（最大兼容老驱动），KHR 扩展入口运行时加载
// - 呈现模式优先 MAILBOX（低延迟），不支持则回退 FIFO
//
//
// ============================================================================

#include <windows.h>
#include <commdlg.h>
#include <initguid.h>
#include <wincodec.h>
#include "vulkan_loader.h"
#include "settings_window.h"
#include "camera.h"
#include "model_import.h"
#include "mc_blocks.h" // 我的世界通用方块库 + 图集 + 合并网格
#include "import_pipeline.h"
#include "import_window.h"
#include "app.h"
#include "ui_button.h" // 共享 GDI 按钮渲染管线（UpdateButtonColor / PointInButton / DrawGdiButton）
#include "ui_presets.h" // 统一控件预设体系：2D UI 面板/描边/图标颜色从 ui::g_theme 取值
#include "resource.h" // 嵌入的球按钮图标 RCDATA 资源 ID
#include "awa_internal.h"
#include "platform/Platform.h"  // GHOST-lite：窗口/表面 OS 边界收口

// 主窗口 platform:: 句柄（P1 创建于 CreateWindowApp；WndProc 内窗口操作统一走此全局，
// 避免每次从 HWND 反查 Window*）。定义在文件末尾 CreateWindowApp 之前，此处前向声明。
extern platform::Window* g_mainWindow;
#include <gear_png.inc>
#include <pen_png.inc>
#include <file_png.inc>
#include <awa_png.inc>
#include <import_png.inc>
#include <export_png.inc>


#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>


using std::uint32_t;

// ---- 3D 模型存储区（用户本轮：MC 加载完成后先存这里，不直接渲染到视口）----
// SceneObject 已通过 model_import.h 引入
struct StoredModel { SceneObject obj; std::wstring label; };
static std::vector<StoredModel> g_mcModelStore;

// 窗口标题：**普通宽字符数组**"窗口显示名称为什么只有 1 个字符"——
// 旧实现 `std::wstring_view` 的 .data 传入 CreateWindowExW 在 MinGW -fshort-wchar
// 下可能被截断为 1 字符；数组形式保证完整 UTF-16 传递
const wchar_t kWindowTitle[] = L"awa";
// 版本号 = 单一来源不硬编码——由 CMakeLists project VERSION 生成
// version_gen.h（VKB_VERSION_STR）；更新版本只改 CMakeLists 顶部 VERSION
#include "version_gen.h"
const wchar_t kVersionW[] = L"" VKB_VERSION_STR "";
// 软件著作人要求
const wchar_t kAuthorW[]  = L"大章鱼用ai做的";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

// ---------------------------------------------------------------------------
// 错误收集：初始化任一环节失败即记录原因，由入口统一弹窗
// ---------------------------------------------------------------------------
std::string g_error;

const char* g_stage = "启动前";

// 按钮文字字体：优先私有加载 资源/NotoSansSC-Regular.otf（OFL 开源），失败回退系统微软雅黑
static wchar_t g_buttonFontName[64] = L"微软雅黑";

void SetError(const std::string& msg) {
    if (g_error.empty()) {
        g_error = msg;
        VkbLog(("[seterror] " + msg).c_str());
    }
}

// 弹窗显示错误信息UTF-8 → 宽字符，用 MessageBoxW 显示，避免 MessageBoxA 把
// UTF-8 中文按系统 ANSI 代码页解释成乱码——"vulkan(乱码)"报错的根因
void ShowErrorBox(const char* utf8Msg) {
    wchar_t wmsg[4096];
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8Msg, -1, wmsg, 4096);
    if (n > 0) MessageBoxW(nullptr, wmsg, L"awa", MB_ICONERROR | MB_OK);
}


// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// （2026-08-19 用户要求"单独创建关于摄像机的 cpp 文件"；含预设视角 + 截图接口）
// ---------------------------------------------------------------------------



// ===========================================================================
// ===========================================================================
// SceneObject 结构定义在 model_import.hOBJ/STL/glTF/FBX 共享

wchar_t g_startupObjPath[MAX_PATH] = L"";

// 渲染开关
// g_useQuads：true=四边形面渲染（性能观感兼具，默认）；false=三角面（兼容）
// g_showNormals：true=渲染法线线（默认关闭，保留接口）
// g_swapZtoY：true=OBJ Z-up → Y-up 旋转（默认；多数 OBJ 文件是 Z-up，旋转后模型正立）
// false=不旋转（适用于已经是 Y-up 的模型，避免旋转 90° 导致模型倾倒）
bool g_useQuads = true;
bool g_showNormals = false;
bool g_swapZtoY = true;



// ---------------------------------------------------------------------------
// 与场景对象（立方体）一起渲染，**从坐标轴/gizmo 管线独立出来**（用户要求）。

// ---------------------------------------------------------------------------

// UpdateButtonColor / PointInButton 已迁入 ui_button.cpp（共享 GDI 按钮渲染管线）

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------





// ==================== 3D 物体选中/线框，Blender 风格====================


// ==================== 移动三向标，Blender 风格====================



// 绘制移动三向标锥头**实体填充、完全不透明**：3 条彩色轴箭头（X 红 / Y 绿 / Z 蓝），
// 无深度测试**渲染于最顶层始终可见**；轴长按相机距离保持恒定屏幕尺寸；拖动中高亮被选轴。
// 顶点写世界坐标（含平移），用 mvp 投影
// 移动三向标——外观统一为缩放 gizmo 样式（3 轴 + 锥头 + 中心方块），


// 旋转 gizmo（Blender 样式）：绕 X 轴（红轴）完整圆环 + 4 象限手柄球 + 中心轴点；
// 拖拽中：弧线箭头显示累计旋转角度（从 0° 到当前角）

// 命中旋转环（环采样 <16px / 手柄球 <18px），命中返回 0，未命中 -1
// 命中旋转环（3 环采样 <16px / 手柄球 <18px），返回命中环轴 0/1/2（X/Y/Z），未命中 -1
// 命中旋转环（3 环采样 <16px / 手柄球 <18px），返回命中环轴 0/1/2（X/Y/Z），未命中 -1
// 三环重叠时优先命中"最正对相机"的环（面向度 = |环法线·视线|），修正优先级错误




// ==================== 选中物体外轮廓描边（视相关 silhouette）====================


// 绘制选中物体外轮廓——黄色 2px 线（深度 LEQUAL，线框模式下白线 pass 已跳过选中物体不会覆盖）。
// 画法优先级：A=视相关 silhouette（正面/背面分界边 + 边界边）；轮廓边过多（>20 万）或无可计算数据时

// ==================== 撤销/重做Ctrl+Z 撤销 / Ctrl+B 重做====================

void PushUndo(App& app, const UndoEntry& e) {
    app.undo.undoStack.push_back(e);
    if (static_cast<int>(app.undo.undoStack.size()) > App::UndoState::kUndoCapacity)
        app.undo.undoStack.erase(app.undo.undoStack.begin()); // 超出容量丢最旧
        app.undo.redoStack.clear(); // 新操作清空重做栈
}

// 撤销/重做后：重建顶点缓冲 + 修正选中索引 + 复位线框/三向标状态
static void RebuildAfterUndoRedo(App& app) {
    vkDeviceWaitIdle(app.vk.device);
    CreateVertexBuffer3D(app);
    if (app.scene.selectedObject >= static_cast<int>(app.scene.objects.size()))
        app.scene.selectedObject = static_cast<int>(app.scene.objects.size()) - 1;
    app.scene.wireframeSel = false;
    app.gizmo.gizmoDragging = false;
    app.gizmo.gizmoAxis = -1;
}

static void Undo(App& app) {
    if (app.undo.undoStack.empty()) return;
    UndoEntry e = app.undo.undoStack.back();
    app.undo.undoStack.pop_back();
    switch (e.op) {
 case UndoOp::Add: // 撤销添加（导入/复制）：移除该物体
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size()))
            app.scene.objects.erase(app.scene.objects.begin() + e.index);
        break;
 case UndoOp::Remove: // 撤销删除：恢复该物体（副本插入，快照保持有效）
        if (e.index >= 0 && e.index <= static_cast<int>(app.scene.objects.size()))
            app.scene.objects.insert(app.scene.objects.begin() + e.index, e.obj);
        break;
 case UndoOp::Move: // 撤销移动：还原操作前位置
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.tx = e.oldTx; o.ty = e.oldTy; o.tz = e.oldTz;
        }
        break;
 case UndoOp::Rotate: // 撤销旋转：还原操作前欧拉角
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.rx = e.oldRx; o.ry = e.oldRy; o.rz = e.oldRz;
        }
        break;
 case UndoOp::Scale: // 撤销缩放：还原操作前缩放
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.sx = e.oldSx; o.sy = e.oldSy; o.sz = e.oldSz;
        }
        break;
    }
    app.undo.redoStack.push_back(e);
    if (static_cast<int>(app.undo.redoStack.size()) > App::UndoState::kRedoCapacity)
        app.undo.redoStack.erase(app.undo.redoStack.begin());
    RebuildAfterUndoRedo(app);
}

static void Redo(App& app) {
    if (app.undo.redoStack.empty()) return;
    UndoEntry e = app.undo.redoStack.back();
    app.undo.redoStack.pop_back();
    switch (e.op) {
 case UndoOp::Add: // 重做添加：有导入路径 → 重新导入操作式，不拷贝大模型；否则快照插入（复制物体）
        if (e.index >= 0 && e.index <= static_cast<int>(app.scene.objects.size())) {
            if (!e.importPath.empty()) {
                SceneObject o;
                try {
                    if (ImportModelFile(e.importPath.c_str(), o)) {
                        o.name = e.name;
                        app.scene.objects.insert(app.scene.objects.begin() + e.index, std::move(o));
                    }
                } catch (const std::bad_alloc&) {
                    SetError("内存不足：撤销/重做重新导入失败（模型过大）。");
                }
            } else {
                app.scene.objects.insert(app.scene.objects.begin() + e.index, e.obj);
            }
        }
        break;
 case UndoOp::Remove: // 重做删除：再次移除
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size()))
            app.scene.objects.erase(app.scene.objects.begin() + e.index);
        break;
 case UndoOp::Move: // 重做移动：还原到操作后位置
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.tx = e.newTx; o.ty = e.newTy; o.tz = e.newTz;
        }
        break;
 case UndoOp::Rotate: // 重做旋转：还原到操作后欧拉角
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.rx = e.newRx; o.ry = e.newRy; o.rz = e.newRz;
        }
        break;
 case UndoOp::Scale: // 重做缩放：还原到操作后缩放
        if (e.index >= 0 && e.index < static_cast<int>(app.scene.objects.size())) {
            SceneObject& o = app.scene.objects[e.index];
            o.sx = e.newSx; o.sy = e.newSy; o.sz = e.newSz;
        }
        break;
    }
    app.undo.undoStack.push_back(e);
    if (static_cast<int>(app.undo.undoStack.size()) > App::UndoState::kUndoCapacity)
        app.undo.undoStack.erase(app.undo.undoStack.begin());
    RebuildAfterUndoRedo(app);
}

static void DeleteSelectedObject(App& app) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    UndoEntry e;
    e.op = UndoOp::Remove;
    e.index = app.scene.selectedObject;
 e.obj = app.scene.objects[app.scene.selectedObject]; // 快照（副本）
    PushUndo(app, e);
    vkDeviceWaitIdle(app.vk.device);
    app.scene.objects.erase(app.scene.objects.begin() + app.scene.selectedObject);
    app.scene.selectedObject = -1;
    app.scene.wireframeSel = false;
    CreateVertexBuffer3D(app);
}

static void DuplicateSelectedObject(App& app) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    vkDeviceWaitIdle(app.vk.device);
    SceneObject copy;
    try {
        copy = app.scene.objects[app.scene.selectedObject];
    } catch (const std::bad_alloc&) {
        SetError("内存不足：无法复制该物体（模型过大）。");
        return;
    }
 copy.tx += 1.5f; // 偏移避免与原件重叠
    copy.name = copy.name + L" (复制)";
    app.scene.objects.push_back(std::move(copy));
    app.scene.selectedObject = static_cast<int>(app.scene.objects.size()) - 1;
    UndoEntry e;
    e.op = UndoOp::Add;
    e.index = app.scene.selectedObject;
 e.obj = app.scene.objects[app.scene.selectedObject]; // 快照（副本）
    PushUndo(app, e);
    CreateVertexBuffer3D(app);
}

// 全屏/还原切换（定义见 CreateWindowApp 之后）
static void ToggleMaximize(App& app);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    { static int s_wm=0; if(s_wm<160){++s_wm; VkbLog(("[wmsg] "+std::to_string((unsigned)msg)+" wp="+std::to_string((unsigned long long)wParam)).c_str());} }
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
 case WM_GETMINMAXINFO: // 最小窗口尺寸（防止拖到过小导致交换链/布局异常）
        if (MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam)) {
            mmi->ptMinTrackSize.x = 320;
            mmi->ptMinTrackSize.y = 240;
        }
        return 0;
    case WM_SIZE:
        if (App* app = GetApp(hwnd)) {
            const uint32_t w = static_cast<uint32_t>(LOWORD(lParam));
            const uint32_t h = static_cast<uint32_t>(HIWORD(lParam));
            if (wParam == SIZE_MINIMIZED) {
                // 最小化：标记并停止渲染，避免最小化窗口上继续操作交换链导致驱动崩溃
                app->ui.minimized = true;
            } else {
                app->ui.minimized = false;
                // Round296：maximized 由"窗口是否正好铺满工作区"推导，不再依赖 wParam。
                // 主窗口为 WS_POPUP 无 WS_MAXIMIZE 样式，程序化 SetWindowPos 最大化后系统
                // 只发 SIZE_RESTORED，旧逻辑会把 maximized 误清 → 最大化按钮"有时失效/无法还原"。
                // 改为与真实几何比较：铺满工作区即视为最大化，NCHITTEST 边缘缩放据此开关。
                RECT wr{}; platform::GetWindowRect(g_mainWindow, &wr);
                RECT mw{}, wk{}; platform::GetWindowMonitorRect(g_mainWindow, &mw, &wk);
                const bool atWork = (wr.left == wk.left && wr.top == wk.top &&
                                     (wr.right - wr.left) == (wk.right - wk.left) &&
                                     (wr.bottom - wr.top) == (wk.bottom - wk.top));
                app->ui.maximized = atWork;
                if (w != app->vk.swapchainExtent.width || h != app->vk.swapchainExtent.height) {
                    app->resizePending = true;
                }
            }
            // 修复：不在 WM_SIZE 内同步渲染。拖拽/缩放时 Windows 洪水般发 WM_SIZE，
            // 每帧同步跑整帧 Vulkan 会饿死消息泵 → 窗口"无响应"；首次显示亦可能在 WM_SIZE
            // 内重入 RecreateSwapchain 触发驱动死锁。改为仅由主循环统一渲染（DrawFrame 已处理 resize）。

        }
        return 0;
 case WM_SETCURSOR: // 悬停即显示对应光标（不依赖 DefWindowProc）：分隔线缩放 / 边缘缩放含四角斜线 / 移动区手势
        if (App* app = GetApp(hwnd)) {
            POINT pt{};
            GetCursorPos(&pt);
            platform::ScreenToClient(g_mainWindow, &pt);
            // 1) 面板分隔线（32645=IDC_SIZENS 上下箭头 / 32644=IDC_SIZEWE 左右箭头）
            const int div = (app->ui.resizeDrag >= 0)
                                ? app->ui.resizeDrag
                                : HitResizeDivider(*app, static_cast<float>(pt.x), static_cast<float>(pt.y));
            if (div >= 0) {
                const int cid = (div == 0 || div == 3) ? 32645 : 32644;
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cid)));
                return TRUE;
            }
            // 2) 窗口边缘缩放（与 NCHITTEST 同一几何；四角用斜线光标 32642/32643）
            const float cxe = static_cast<float>(pt.x);
            const float cye = static_cast<float>(pt.y);
            const int We = static_cast<int>(app->vk.swapchainExtent.width);
            const int He = static_cast<int>(app->vk.swapchainExtent.height);
            if (cye >= static_cast<float>(app->ui.panelTopH) && We > 0 && He > 0) {
                const int bz = 6;
                const bool left   = cxe <  bz;
                const bool right  = cxe >  We - bz;
                const bool bottom = cye >  He - bz;
                int cid = 0;
                if (bottom && left)        cid = 32643;  // IDC_SIZENESW 左下↗右上斜线
                else if (bottom && right)  cid = 32642;  // IDC_SIZENWSE 右下↖左上斜线
                else if (left || right)    cid = 32644;  // IDC_SIZEWE
                else if (bottom)           cid = 32645;  // IDC_SIZENS
                if (cid) { SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cid))); return TRUE; }
            }
            // 3) 窗口移动检测区（顶栏最上 9px 空白）→ 手势（32649=IDC_HAND）。
            // 按鼠标位置判定（不依赖 wParam：部分系统/时序下 WM_SETCURSOR 的 wParam 不可靠）
            if (cye >= 0.0f && cye < 9.0f) {
                bool onCtrl = PointInButton(app->ui.appIcon, cxe, cye);
                for (auto& b : app->ui.buttons) if (PointInButton(b, cxe, cye)) onCtrl = true;
                for (int i = 0; i < 3 && !onCtrl; ++i)
                    if (PointInButton(app->ui.sysButtons[i], cxe, cye)) onCtrl = true;
                if (!onCtrl) {
                    static int dbgCursor = 0;
                    if (dbgCursor++ < 10) VkbLog("[dbg] SETCURSOR HTCAPTION");
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
                    return TRUE;
                }
            }
        }
 return DefWindowProc(hwnd, msg, wParam, lParam); // 其余：默认箭头（本函数所有 case 均 return，不用 break）
    case WM_KEYDOWN:
        if (App* app = GetApp(hwnd)) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_DELETE) {
 DeleteSelectedObject(*app); // Delete：删除选中
            } else if (wParam == 'D' && ctrl) {
 DuplicateSelectedObject(*app); // Ctrl+D：复制选中
            } else if (wParam == 'Z' && ctrl) {
                Undo(*app); // Ctrl+Z：撤销
            } else if (wParam == 'B' && ctrl) {
                Redo(*app); // Ctrl+B：重做
            } else if (wParam == VK_TAB) {
                if (app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
 app->scene.wireframeSel = !app->scene.wireframeSel; // Tab：线框预览
                    if (app->scene.wireframeSel && app->scene.objects[app->scene.selectedObject].wireVerts.empty()) {
                        BuildObjectWireframe(app->scene.objects[app->scene.selectedObject]);
                        vkDeviceWaitIdle(app->vk.device);
                        CreateVertexBuffer3D(*app);
                    }
                }
            } else if (wParam == 'E' && !ctrl) {
 app->ui.gizmoMode = 0; // E=移动物体
            } else if (wParam == 'R' && !ctrl) {
 app->ui.gizmoMode = 1; // R=旋转物体
            } else if (wParam == 'T' && !ctrl) {
 app->ui.gizmoMode = 2; // T=缩放物体
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (App* app = GetApp(hwnd)) {
            // 鼠标在右部物体栏区域 → 滚动列表（物体数超出 5 栏时）；否则相机缩放
            POINT pt{}; GetCursorPos(&pt);
            platform::ScreenToClient(g_mainWindow, &pt);
            const float mx = static_cast<float>(pt.x), my = static_cast<float>(pt.y);
            const Layout layW = ComputeLayout(*app);
            if (layW.right.extent.width >= 40 && layW.right.extent.height >= 100) {
                const int lpx = layW.right.offset.x + kObjPanelPad;
                const int lpy = layW.right.offset.y + kObjPanelPad + kObjTitleH;
                const int lpW = static_cast<int>(layW.right.extent.width) - 2 * kObjPanelPad;
                const int lpH = kObjMaxRows * (kObjPanelRowH + kObjRowGap);
                if (mx >= lpx && mx < lpx + lpW && my >= lpy && my < lpy + lpH) {
                    const int wheel = GET_WHEEL_DELTA_WPARAM(wParam);
                    const int nAll = static_cast<int>(app->scene.objects.size());
                    const int maxS = (nAll > kObjMaxRows) ? (nAll - kObjMaxRows) : 0;
                    app->ui.objScroll = std::max(0, std::min(maxS, app->ui.objScroll - (wheel > 0 ? 1 : -1)));
                    return 0;  // 滚动列表，不做相机缩放
                }
            }
 // 滚轮缩放相机视角 → 显示左下角距离文字
            const int wheel = GET_WHEEL_DELTA_WPARAM(wParam);
            app->ui.navLastActionMs = GetTickCount64();
            app->ui.navLastActionType = 2;
            app->camera.Zoom(static_cast<float>(wheel) / WHEEL_DELTA);
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        // 双击改名已并入 WM_LBUTTONUP 的 0.2s 判定（双击 = 两次 LBUTTONDOWN/UP，第一击不选中第二击改名）；
        // 此处吞掉 DBLCLK，避免 Windows 双击消息与两次 UP 处理冲突（双重触发改名）。
        return 0;
    case WM_LBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
            const float mx = MouseX(lParam);
            const float my = MouseY(lParam);
            if (app->ui.menuOpen) {
                for (int i = 0; i < 2; ++i) {
                    if (app->ui.menuItems[i].machine.OnMouseDown(PointInButton(app->ui.menuItems[i], mx, my))) {
                        app->ui.pressedMenuItem = i;
                        platform::SetCapture(g_mainWindow);  // 捕获鼠标：移出窗口后释放左键仍可达，动画不滞留
                        return 0;
                    }
                }
                constexpr float kMenuPad = 10.0f;
                constexpr float kMenuItemH = 30.0f;
                const float menuH = kMenuPad * 2.0f + kMenuItemH;
                if (mx >= static_cast<float>(app->ui.menuRect.offset.x) &&
                    mx <  static_cast<float>(app->ui.menuRect.offset.x + static_cast<int32_t>(app->ui.menuRect.extent.width)) &&
                    my >= static_cast<float>(app->ui.menuRect.offset.y) &&
                    my <  static_cast<float>(app->ui.menuRect.offset.y) + menuH) {
                    return 0;
                }
            }
 // 分隔线拖拽开始（优先于按钮/物体拾取；命中 4 条可缩放边缘）
            const int divider = HitResizeDivider(*app, mx, my);
            if (divider >= 0) {
                app->ui.resizeDrag = divider;
                app->ui.resizeStartMouse = (divider == 1 || divider == 2) ? mx : my;
                app->ui.resizeStartVal = (divider == 0) ? app->ui.panelTopH :
                                      (divider == 1) ? app->ui.panelLeftW :
                                      (divider == 2) ? app->ui.panelRightW : app->ui.panelBottomH;
                app->scene.mouseDragged = true;
                platform::SetCapture(g_mainWindow);
                return 0;
            }
            // 左上角软件图标（特殊按钮，无边框）→ 打开设置
            if (app->ui.appIcon.machine.OnMouseDown(PointInButton(app->ui.appIcon, mx, my))) {
                app->ui.appIconDown = true;
                app->ui.menuOpen = false;
                platform::SetCapture(g_mainWindow);
                return 0;
            }
            // 右上角系统按钮：最小化/最大化/关闭（按下记录，松开会触发动作）
            {
                int sysHit = -1;
                for (int i = 0; i < 3; ++i) {
                    if (app->ui.sysButtons[i].machine.OnMouseDown(PointInButton(app->ui.sysButtons[i], mx, my))) {
                        sysHit = i;
                        break;
                    }
                }
                if (sysHit >= 0) {
                    app->ui.pressedSys = sysHit;
                    platform::SetCapture(g_mainWindow);  // 捕获鼠标确保 LBUTTONUP 可靠到达（即使微移）
                    return 0;
                }
            }
            int hit = -1;
            for (size_t i = 0; i < app->ui.buttons.size(); ++i) {
                if (app->ui.buttons[i].machine.OnMouseDown(PointInButton(app->ui.buttons[i], mx, my))) {
                    hit = static_cast<int>(i);
                    break;
                }
            }
            if (hit >= 0) {
                app->ui.pressedButton = hit;
                platform::SetCapture(g_mainWindow);  // 捕获鼠标：按钮动画不因移出窗口而停滞
                if (app->ui.buttons[hit].icon != 1) app->ui.menuOpen = false;
            } else {
                app->ui.menuOpen = false;
                // 顶栏空白（非控件）→ 不触发 orbit/拾取（拖拽/还原由 WM_NCHITTEST 处理）
                if (my < static_cast<float>(app->ui.panelTopH)) return 0;
 // 右侧物体列表点击选中Blender 风格多物体编辑栏；跟随可调右栏布局
 // 方案B：列表区起点在标题条（kObjTitleH）之下，行步进含 kObjRowGap 间距
                const Layout layR = ComputeLayout(*app);
                if (layR.right.extent.width >= 40 && layR.right.extent.height >= 100) {
                    const int px = layR.right.offset.x + kObjPanelPad;
                    const int py = layR.right.offset.y + kObjPanelPad + kObjTitleH;
                    const int pw = static_cast<int>(layR.right.extent.width) - 2 * kObjPanelPad;
                    const int ph = kObjMaxRows * (kObjPanelRowH + kObjRowGap);  // 固定约 5 栏行区域
                    if (mx >= px && mx < px + pw && my >= py && my < py + ph) {
                        // 左键按下：记录框选起点，等待移动判定（>4px 进入框选）；不立即选中
                        // （选中/重命名在松开时按 0.2s 双击判定处理）
                        app->ui.objMarqueePending = true;
                        app->ui.objMx0 = mx; app->ui.objMy0 = my;
                        app->ui.objMx1 = mx; app->ui.objMy1 = my;
 app->scene.mouseDragged = true; // 屏蔽松开时的物体拾取
                        platform::SetCapture(g_mainWindow);
                        return 0;
                    }
                }
 // 左上角 3 个圆形按钮标准按钮动画
                for (int i = 0; i < 3; ++i) {
                    if (app->ui.ballButtons[i].machine.OnMouseDown(PointInButton(app->ui.ballButtons[i], mx, my))) {
                        app->ui.pressedBall = i;
                        platform::SetCapture(g_mainWindow);
 app->scene.mouseDragged = true; // 屏蔽松开时的物体拾取
                        return 0;
                    }
                }
 // 旋转模式（gizmoMode==1）——命中三色环/手柄球才拖拽（Blender 式：未命中不拦截，可正常 orbit 视角）
                if (app->ui.gizmoMode == 1 && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                    const int ringAxis = PickRotateGizmoAt(*app, mx, my);
 if (ringAxis != -1) { // 0/1/2=环轴，-2=中心轴点（trackball 自由旋转）
                        SceneObject& so = app->scene.objects[app->scene.selectedObject];
 app->gizmo.gizmoAxis = ringAxis; // 0/1/2 = X/Y/Z 环（红/绿/蓝）
 app->gizmo.gizmoDragMode = 4; // 4=旋转拖拽
                        app->gizmo.gizmoDragging = true;
                        app->gizmo.gizmoStartRx = so.rx; app->gizmo.gizmoStartRy = so.ry; app->gizmo.gizmoStartRz = so.rz;
                        app->scene.pressX = mx; app->scene.pressY = my;
 // 记录枢轴的屏幕投影（旋转角度基准，沿环切向拖拽）
                        {
                            const Layout lay2 = ComputeLayout(*app);
                            const VkRect2D& vp2 = lay2.viewport;
                            float view[16], proj[16], mvp[16];
                            app->camera.ViewMatrix(view);
                            const float aspect2 = static_cast<float>(vp2.extent.width) /
                                                  static_cast<float>(vp2.extent.height);
                            app->camera.ProjectionMatrix(aspect2, proj);
                            MatMul4(proj, view, mvp);
                            const VkViewport gvp2{static_cast<float>(vp2.offset.x),
                                                  static_cast<float>(vp2.offset.y),
                                                  static_cast<float>(vp2.extent.width),
                                                  static_cast<float>(vp2.extent.height), 0.0f, 1.0f};
                            float gp[3];
                            GizmoPivot(so, gp);
                            if (!ProjectToViewport(mvp, gvp2, gp[0], gp[1], gp[2],
                                                   app->gizmo.gizmoScreenPivotX, app->gizmo.gizmoScreenPivotY)) {
                                app->gizmo.gizmoScreenPivotX = mx;
                                app->gizmo.gizmoScreenPivotY = my;
                            }
                        }
                        app->scene.mouseDragged = true;
                        platform::SetCapture(g_mainWindow);
                        return 0;
                    }
                }
 // 缩放模式（gizmoMode==2）——命中缩放轴/中心 → 拖拽等比缩放
                if (app->ui.gizmoMode == 2 && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                    const int axis = PickScaleGizmoAt(*app, mx, my);
                    if (axis >= 0) {
                        SceneObject& so = app->scene.objects[app->scene.selectedObject];
 app->gizmo.gizmoAxis = axis; // 0/1/2=轴 3=中心（等比）
 app->gizmo.gizmoDragMode = 5; // 5=缩放拖拽
                        app->gizmo.gizmoDragging = true;
                        app->gizmo.gizmoStartSx = so.sx; app->gizmo.gizmoStartSy = so.sy; app->gizmo.gizmoStartSz = so.sz;
                        app->scene.pressX = mx; app->scene.pressY = my;
                        app->scene.mouseDragged = true;
                        platform::SetCapture(g_mainWindow);
                        return 0;
                    }
                }
 // 移动三向标抓取（选中物体时优先于 orbit；仅移动模式 gizmoMode==0 命中）
                if (app->ui.gizmoMode == 0 && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
 // 中心环Blender 拖中心球 → 视口平面任意方向自由移动。
 // 白边（选中框边线）拖拽已按用户要求**删除**，不再作为移动把手。
                    if (HitGizmoRingAt(*app, mx, my)) {
                        StartFreeDrag(*app, hwnd, mx, my);
                        return 0;
                    }
 // 命中某根轴 → 沿轴拖拽
                    const int axis = PickGizmoAxisAt(*app, mx, my);
                    if (axis >= 0) {
                        SceneObject& so = app->scene.objects[app->scene.selectedObject];
                        app->gizmo.gizmoAxis = axis;
                        app->gizmo.gizmoDragMode = 1;
                        app->gizmo.gizmoDragging = true;
                        app->gizmo.gizmoStartTx = so.tx; app->gizmo.gizmoStartTy = so.ty; app->gizmo.gizmoStartTz = so.tz;
                        GizmoPivot(so, app->gizmo.gizmoPivot);
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）
                        float o[3], d[3];
                        float t = 0.0f;
                        if (BuildViewRay(*app, mx, my, o, d)) ClosestAxisParam(app->gizmo.gizmoPivot, dirs[axis], o, d, t);
                        app->gizmo.gizmoStartT = t;
 app->scene.pressX = mx; // 记录按下位置（区分点击/拖拽）
                        app->scene.pressY = my;
 app->scene.mouseDragged = true; // 三向标按下即视为拖拽：松开时不重新拾取（防误取消选中）
                        platform::SetCapture(g_mainWindow);
                        return 0;
                    }
                }
 // 左键 = 移动视角（orbit，原中键功能）——空白处按下记录，**移动>4px 才进入 orbit**
 // （gizmo/UI 命中已在上方 return，此处仅空白处）
                app->camera.orbiting = false;
                app->camera.lastX = mx;
                app->camera.lastY = my;
 app->ui.lbuttonDown = true; // 左键按住标志（MOUSEMOVE 检测移动>4px → orbit）
 app->scene.pressX = mx; // 记录按下位置（区分点击/拖拽）
                app->scene.pressY = my;
                app->scene.mouseDragged = false;
                platform::SetCapture(g_mainWindow);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (App* app = GetApp(hwnd)) {
 // 自定义标题栏拖拽结束：释放捕获 + 边缘吸附（仅 Win10- 自实现；Win11+ 由系统原生 Snap 处理）
            if (app->ui.captionDragging) {
                app->ui.captionDragging = false;
                platform::ReleaseCapture();
                // 最大化下延后还原但 <25px 释放 → 视为单击，不还原不吸附
                if (app->ui.pendingRestore) {
                    app->ui.pendingRestore = false;
                    app->resizePending = true;
                    return 0;
                }
                if (!platform::IsWindows11OrLater()) {
                    RECT wr{}, mw{}, wk{};
                    platform::GetWindowRect(g_mainWindow, &wr);
                    platform::GetWindowMonitorRect(g_mainWindow, &mw, &wk);
                    const int snap = 12;
                    if (wr.top <= mw.top + snap) {
                        platform::GetWindowRect(g_mainWindow, &app->ui.normalRect);
                        app->ui.maximized = true;
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.left, wk.top,
                                     wk.right - wk.left, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    } else if (wr.left <= mw.left + snap) {
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.left, wk.top,
                                     (wk.right - wk.left) / 2, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    } else if (wr.right >= mw.right - snap) {
                        const int hw = (wk.right - wk.left) / 2;
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.right - hw, wk.top,
                                     hw, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    }
                }
                app->resizePending = true;  // 拖拽结束统一重建一次（拖拽中已暂停重建）
                return 0;
            }
 // 自定义边缘缩放结束：仅释放捕获（尺寸已在 WM_MOUSEMOVE 实时更新）
            if (app->ui.edgeResizing) {
                app->ui.edgeResizing = false;
                app->ui.resizeEdge    = 0;
                platform::ReleaseCapture();
                app->resizePending = true;  // 缩放结束重建 swapchain（拖拽中已暂停）
                return 0;
            }
 // 物体栏左键松开：框选结算 / 单击 0.2s 双击判定
            if (app->ui.objMarqueePending || app->ui.objMarquee) {
                app->ui.objMarqueePending = false;
                const float ux = MouseX(lParam), uy = MouseY(lParam);
                const Layout layU = ComputeLayout(*app);
                if (app->ui.objMarquee) {
                    app->ui.objMarquee = false;
                    app->ui.objClickRow = -1;  // 框选完成：取消未决的单击判定
                    const int px = layU.right.offset.x + kObjPanelPad;
                    const int py = layU.right.offset.y + kObjPanelPad + kObjTitleH;
                    const int pw = static_cast<int>(layU.right.extent.width) - 2 * kObjPanelPad;
                    const float x0 = std::min(app->ui.objMx0, ux), x1 = std::max(app->ui.objMx0, ux);
                    const float y0 = std::min(app->ui.objMy0, uy), y1 = std::max(app->ui.objMy0, uy);
                    if (x1 - x0 >= 4.0f && y1 - y0 >= 4.0f) {
                        // 框选结算：行矩形与框选矩形相交 → 多选（主操作对象 = 最后一个框内行）
                        app->ui.multiSel.clear();
                        int lastSel = -1;
                        for (int vi = 0; vi < kObjMaxRows; ++vi) {
                            const int row = app->ui.objScroll + vi;
                            if (row >= static_cast<int>(app->scene.objects.size())) break;
                            const float ry0 = static_cast<float>(py + vi * (kObjPanelRowH + kObjRowGap));
                            const float ry1 = ry0 + kObjPanelRowH;
                            const float rx0 = static_cast<float>(px), rx1 = static_cast<float>(px + pw);
                            if (rx0 <= x1 && rx1 >= x0 && ry0 <= y1 && ry1 >= y0) {
                                app->ui.multiSel.push_back(row);
                                lastSel = row;
                            }
                        }
                        app->scene.wireframeSel = false;
                        if (lastSel >= 0) {
                            app->scene.selectedObject = lastSel;  // 主操作对象 = 最后一个框内行
                        } else {
                            app->scene.selectedObject = -1;
                            app->ui.multiSel.clear();
                        }
                    }
                } else {
                    // 单击（未拖拽）：0.2s 判定——第一击不选中；0.2s 内第二击 → 重命名
                    const int px = layU.right.offset.x + kObjPanelPad;
                    const int py = layU.right.offset.y + kObjPanelPad + kObjTitleH;
                    const int pw = static_cast<int>(layU.right.extent.width) - 2 * kObjPanelPad;
                    const int ph = kObjMaxRows * (kObjPanelRowH + kObjRowGap);
                    if (ux >= px && ux < px + pw && uy >= py && uy < py + ph) {
                        const int row = static_cast<int>((uy - py) / (kObjPanelRowH + kObjRowGap)) + app->ui.objScroll;
                        const uint64_t nowMs = GetTickCount64();
                        if (app->ui.objClickRow == row && nowMs - app->ui.objClickMs < 400) {
                            app->ui.objClickRow = -1;   // 0.2s 内第二击 → 重命名
                            OpenRenameEdit(*app, row);
                        } else {
                            app->ui.objClickRow = row;  // 第一击：记录待判定（不立即选中）
                            app->ui.objClickMs = nowMs;
                        }
                    } else {
                        app->ui.objClickRow = -1;  // 点在物体栏外：取消待判定
                    }
                }
                platform::ReleaseCapture();
                return 0;
            }
 // 分隔线拖拽结束（尺寸已实时更新）
            if (app->ui.resizeDrag >= 0) {
                app->ui.resizeDrag = -1;
                platform::ReleaseCapture();
 // 拖拽结束 → 面板尺寸写入 awa_settings.txt（顶栏固定不保存；下次启动恢复左/右/底）
                SaveSettingInt("panel_left_w", static_cast<int>(app->ui.panelLeftW));
                SaveSettingInt("panel_right_w", static_cast<int>(app->ui.panelRightW));
                SaveSettingInt("panel_bottom_h", static_cast<int>(app->ui.panelBottomH));
            }
 // 三向标/中心环拖拽结束（松开即提交；位置有变化才记录撤销项）
            if (app->gizmo.gizmoDragging) {
                app->gizmo.gizmoDragging = false;
 // 旋转拖拽结束（gizmoDragMode==4）——有变化才记录撤销
                if (app->gizmo.gizmoDragMode == 4 && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                    SceneObject& so = app->scene.objects[app->scene.selectedObject];
                    if (so.rx != app->gizmo.gizmoStartRx || so.ry != app->gizmo.gizmoStartRy || so.rz != app->gizmo.gizmoStartRz) {
                        UndoEntry e;
                        e.op = UndoOp::Rotate;
                        e.index = app->scene.selectedObject;
                        e.oldRx = app->gizmo.gizmoStartRx; e.oldRy = app->gizmo.gizmoStartRy; e.oldRz = app->gizmo.gizmoStartRz;
                        e.newRx = so.rx;              e.newRy = so.ry;              e.newRz = so.rz;
                        PushUndo(*app, e);
                    }
                }
 // 缩放拖拽结束（gizmoDragMode==5）——有变化才记录撤销
                if (app->gizmo.gizmoDragMode == 5 && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                    SceneObject& so = app->scene.objects[app->scene.selectedObject];
                    if (so.sx != app->gizmo.gizmoStartSx || so.sy != app->gizmo.gizmoStartSy || so.sz != app->gizmo.gizmoStartSz) {
                        UndoEntry e;
                        e.op = UndoOp::Scale;
                        e.index = app->scene.selectedObject;
                        e.oldSx = app->gizmo.gizmoStartSx; e.oldSy = app->gizmo.gizmoStartSy; e.oldSz = app->gizmo.gizmoStartSz;
                        e.newSx = so.sx;              e.newSy = so.sy;              e.newSz = so.sz;
                        PushUndo(*app, e);
                    }
                }
                if ((app->gizmo.gizmoAxis >= 0 || app->gizmo.gizmoDragMode == 3) && app->scene.selectedObject >= 0 &&
                    app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                    SceneObject& so = app->scene.objects[app->scene.selectedObject];
                    if (so.tx != app->gizmo.gizmoStartTx || so.ty != app->gizmo.gizmoStartTy || so.tz != app->gizmo.gizmoStartTz) {
                        UndoEntry e;
                        e.op = UndoOp::Move;
                        e.index = app->scene.selectedObject;
                        e.oldTx = app->gizmo.gizmoStartTx; e.oldTy = app->gizmo.gizmoStartTy; e.oldTz = app->gizmo.gizmoStartTz;
                        e.newTx = so.tx;              e.newTy = so.ty;              e.newTz = so.tz;
                        PushUndo(*app, e);
                    }
                }
                app->gizmo.gizmoAxis = -1;
                app->gizmo.gizmoDragMode = 0;
                app->gizmo.gizmoLastHitValid = false;
            }
 // 左上角圆形按钮释放；球1=线框模式 球2=实体模式 球3暂定
            if (app->ui.pressedBall >= 0) {
                const int idx = app->ui.pressedBall;
                app->ui.pressedBall = -1;
                platform::ReleaseCapture();  // 与 LBUTTONDOWN 的 SetCapture 配对
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ui.ballButtons[idx], ux, uy);
                app->ui.ballButtons[idx].machine.OnMouseUp(inside);
                if (inside) {
 if (idx == 0) app->ui.renderMode = 1; // 线框模式
 else if (idx == 1) app->ui.renderMode = 0; // 实体模式
 // idx==2：暂定（无功能）
                }
            }
            if (app->ui.pressedMenuItem >= 0) {
                const int idx = app->ui.pressedMenuItem;
                app->ui.pressedMenuItem = -1;
                platform::ReleaseCapture();  // 与 LBUTTONDOWN 的 SetCapture 配对
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ui.menuItems[idx], ux, uy);
                app->ui.menuItems[idx].machine.OnMouseUp(inside);
                if (inside && app->ui.menuItems[idx].onClick) app->ui.menuItems[idx].onClick(*app);
            }
            if (app->ui.pressedButton >= 0) {
                const int idx = app->ui.pressedButton;
                app->ui.pressedButton = -1;
                platform::ReleaseCapture();  // 与 LBUTTONDOWN 的 SetCapture 配对
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ui.buttons[idx], ux, uy);
                app->ui.buttons[idx].machine.OnMouseUp(inside);
                if (inside) {
 // 顶栏后 3 个按钮=变换模式（移动/旋转/缩放）——按按钮索引设置 gizmoMode
                    if (app->ui.buttons.size() >= 3 &&
                        idx >= static_cast<int>(app->ui.buttons.size()) - 3) {
                        app->ui.gizmoMode = idx - (static_cast<int>(app->ui.buttons.size()) - 3);
                    }
                    if (app->ui.buttons[idx].onClick) app->ui.buttons[idx].onClick(*app);
                }
            }
            // 软件图标释放 → 打开设置（onClick）
            if (app->ui.appIconDown) {
                app->ui.appIconDown = false;
                platform::ReleaseCapture();  // 与 LBUTTONDOWN 的 SetCapture 配对
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ui.appIcon, ux, uy);
                app->ui.appIcon.machine.OnMouseUp(inside);
                if (inside && app->ui.appIcon.onClick) app->ui.appIcon.onClick(*app);
            }
            // 系统按钮释放 → 最小化/最大化/关闭
            if (app->ui.pressedSys >= 0) {
                const int idx = app->ui.pressedSys;
                app->ui.pressedSys = -1;
                platform::ReleaseCapture();  // 释放鼠标捕获（与 LBUTTONDOWN 的 SetCapture 配对）
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ui.sysButtons[idx], ux, uy);
                app->ui.sysButtons[idx].machine.OnMouseUp(inside);
                if (inside) {
                    if (idx == 0) platform::ShowWindow(g_mainWindow, SW_MINIMIZE);
                    else if (idx == 1) ToggleMaximize(*app);
                    else if (idx == 2) platform::DestroyWindow(g_mainWindow);  // → WM_DESTROY → PostQuitMessage
                }
            }
 // 左键 = 移动视角（orbit，原中键功能）——松开结束 orbit 视角拖动
            app->camera.orbiting = false;
            app->ui.lbuttonDown = false;
 // 左键【点击】（非拖拽）且未点按钮 → 3D 视口拾取物体
            if (!app->scene.mouseDragged && app->ui.pressedButton < 0 && app->ui.pressedMenuItem < 0) {
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const Layout lay = ComputeLayout(*app);
                const VkRect2D& vp = lay.viewport;
                if (ux >= static_cast<float>(vp.offset.x) &&
                    ux <  static_cast<float>(vp.offset.x + vp.extent.width) &&
                    uy >= static_cast<float>(vp.offset.y) &&
                    uy <  static_cast<float>(vp.offset.y + vp.extent.height)) {
                    const int picked = PickObjectAt(*app, ux, uy);
 // 单击单选 → 清空框选多选
                    app->ui.multiSel.clear();
                    // 再次点击已选中物体 → 取消选中
                    if (picked == app->scene.selectedObject && picked >= 0) {
                        app->scene.selectedObject = -1;
                        app->scene.wireframeSel = false;
                    } else {
                        app->scene.selectedObject = picked;
                        if (app->scene.selectedObject < 0) app->scene.wireframeSel = false;
                    }
                }
            }
        }
        platform::ReleaseCapture();
        return 0;
    case WM_MBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
 // 中键 = 框选目标（原左键功能）——**任意位置按下都触发**（含 2D UI 面板）
 // 框选矩形渲染优先级最高（画在 2D UI 之后），不被面板遮挡
            app->ui.menuOpen = false;
            app->ui.marqueeSelecting = true;
            app->ui.marqueeX0 = MouseX(lParam); app->ui.marqueeY0 = MouseY(lParam);
            app->ui.marqueeX1 = app->ui.marqueeX0; app->ui.marqueeY1 = app->ui.marqueeY0;
 app->scene.pressX = app->ui.marqueeX0; // 记录按下位置（区分点击/框选拖拽）
            app->scene.pressY = app->ui.marqueeY0;
            app->scene.mouseDragged = false;
            platform::SetCapture(g_mainWindow);
        }
        return 0;
    case WM_MBUTTONUP:
        if (App* app = GetApp(hwnd)) {
 // 中键 = 框选（原左键功能），松开完成框选
            if (app->ui.marqueeSelecting) {
                app->ui.marqueeSelecting = false;
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const float x0 = std::min(app->ui.marqueeX0, ux);
                const float y0 = std::min(app->ui.marqueeY0, uy);
                const float x1 = std::max(app->ui.marqueeX0, ux);
                const float y1 = std::max(app->ui.marqueeY0, uy);
                if (x1 - x0 >= 4.0f && y1 - y0 >= 4.0f) {
 // 框选：物体中心（世界平移位置）投影到屏幕，在框内 → 选中（主操作对象=最后一个）
                    app->ui.multiSel.clear();
                    const Layout lay = ComputeLayout(*app);
                    const VkRect2D& vp = lay.viewport;
                    float view[16], proj[16], mvp[16];
                    app->camera.ViewMatrix(view);
                    const float aspect = static_cast<float>(vp.extent.width) /
                                         static_cast<float>(vp.extent.height);
                    app->camera.ProjectionMatrix(aspect, proj);
                    MatMul4(proj, view, mvp);
                    const VkViewport gvp{static_cast<float>(vp.offset.x),
                                          static_cast<float>(vp.offset.y),
                                          static_cast<float>(vp.extent.width),
                                          static_cast<float>(vp.extent.height), 0.0f, 1.0f};
                    int lastSel = -1;
                    for (int i = 0; i < static_cast<int>(app->scene.objects.size()); ++i) {
                        const SceneObject& o = app->scene.objects[i];
                        float sx, sy;
                        if (ProjectToViewport(mvp, gvp, o.tx, o.ty, o.tz, sx, sy)) {
                            if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {
                                app->ui.multiSel.push_back(i);
                                lastSel = i;
                            }
                        }
                    }
                    app->scene.wireframeSel = false;
                    if (lastSel >= 0) {
 app->scene.selectedObject = lastSel; // 主操作对象 = 最后一个框内物体
                    } else {
 app->scene.selectedObject = -1; // 框到空白 → 取消选择
                        app->ui.multiSel.clear();
                    }
 app->scene.mouseDragged = true; // 框选完成：跳过单击拾取
                }
 // 框太小（中键单击）→ 不框选、不拾取（中键语义=框选工具，单击无效）
            }
        }
        platform::ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
            app->ui.menuOpen = false;
            app->camera.panning = true;
            app->camera.lastX = MouseX(lParam);
            app->camera.lastY = MouseY(lParam);
            platform::SetCapture(g_mainWindow);
        }
        return 0;
    case WM_RBUTTONUP:
        if (App* app = GetApp(hwnd)) app->camera.panning = false;
        platform::ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (App* app = GetApp(hwnd)) {
            // 自定义标题栏拖拽：移动窗口（不依赖 DefWindowProc 原生模态循环）
            if (app->ui.captionDragging) {
                POINT p{}; GetCursorPos(&p);
                const int dx = p.x - app->ui.dragStartX;
                const int dy = p.y - app->ui.dragStartY;
                // 最大化下延后还原：累计位移 ≥25px 一次性还原 normalRect，之后正常移动窗口
                if (app->ui.pendingRestore) {
                    if (dx * dx + dy * dy >= 625) {  // 25*25
                        app->ui.maximized = false;
                        app->ui.pendingRestore = false;
                        platform::SetWindowPos(g_mainWindow, nullptr,
                            app->ui.normalRect.left, app->ui.normalRect.top,
                            app->ui.normalRect.right - app->ui.normalRect.left,
                            app->ui.normalRect.bottom - app->ui.normalRect.top,
                            SWP_NOZORDER | SWP_FRAMECHANGED);
                        platform::GetWindowRect(g_mainWindow, &app->ui.dragOrigRect);
                        // 还原后重定起点，避免窗口跳到原最大化的鼠标偏移量
                        app->ui.dragStartX = p.x;
                        app->ui.dragStartY = p.y;
                        // 首次还原后立即按当前位移设置位置
                        const int nx2 = app->ui.dragOrigRect.left + (p.x - app->ui.dragStartX);
                        const int ny2 = app->ui.dragOrigRect.top  + (p.y - app->ui.dragStartY);
                        platform::SetWindowPos(g_mainWindow, nullptr, nx2, ny2, 0, 0,
                                               SWP_NOSIZE | SWP_NOZORDER);
                    }
                    // <25px 期间：窗口不动（仍在最大化），等待用户继续拖动决定
                    return 0;
                }
                const int nx = app->ui.dragOrigRect.left + dx;
                const int ny = app->ui.dragOrigRect.top  + dy;
                platform::SetWindowPos(g_mainWindow, nullptr, nx, ny, 0, 0,
                                       SWP_NOSIZE | SWP_NOZORDER);
                return 0;
            }
            // 物体栏左键框选：按下后移动 >4px 进入框选模式（绘制/结算见 DrawLogicBar 与 LBUTTONUP）
            if (app->ui.objMarqueePending && !app->ui.objMarquee) {
                const float mxc = MouseX(lParam), myc = MouseY(lParam);
                const float dxm = mxc - app->ui.objMx0, dym = myc - app->ui.objMy0;
                if (dxm * dxm + dym * dym >= 16.0f) app->ui.objMarquee = true;  // 位移 ≥4px
            }
            if (app->ui.objMarquee) {
                app->ui.objMx1 = MouseX(lParam);
                app->ui.objMy1 = MouseY(lParam);
                return 0;
            }
            // 自定义边缘缩放：按边缘调整窗口尺寸（实时更新，释放时不再重算尺寸）
            if (app->ui.edgeResizing) {
                static int dbgResize = 0;
                if (dbgResize++ < 5)
                    VkbLog(("[dbg] RESIZE edge=" + std::to_string(app->ui.resizeEdge)).c_str());
                POINT p{}; GetCursorPos(&p);
                const int dx = p.x - app->ui.dragStartX;
                const int dy = p.y - app->ui.dragStartY;
                const RECT& o = app->ui.dragOrigRect;
                const int minW = 320, minH = 240;
                int nx = o.left, ny = o.top, nw = o.right - o.left, nh = o.bottom - o.top;
                switch (app->ui.resizeEdge) {
                    case 1: nx = o.left + dx; nw = o.right - nx; break;
                    case 2: nw = (o.right - o.left) + dx; break;
                    case 3: nh = (o.bottom - o.top) + dy; break;
                    case 4: nx = o.left + dx; nw = o.right - nx; nh = (o.bottom - o.top) + dy; break;
                    case 5: nw = (o.right - o.left) + dx; nh = (o.bottom - o.top) + dy; break;
                }
                if (nw < minW) { if (app->ui.resizeEdge == 1 || app->ui.resizeEdge == 4) nx = o.right - minW; nw = minW; }
                if (nh < minH) { nh = minH; }
                // 捕获期间 WM_SETCURSOR 不触发，主动设置缩放光标（32644=WE 32645=NS）
                const int cid = (app->ui.resizeEdge == 1 || app->ui.resizeEdge == 2) ? 32644 : 32645;
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cid)));
                platform::SetWindowPos(g_mainWindow, nullptr, nx, ny, nw, nh,
                                       SWP_NOZORDER | SWP_FRAMECHANGED);
                return 0;
            }
            const float x = MouseX(lParam);
            const float y = MouseY(lParam);
 app->gizmo.mouseX = x; // 悬停高亮（中心环）用
            app->gizmo.mouseY = y;
 // 分隔线拖拽——实时调整面板尺寸（min=默认值，max 保证视口 >=60px）
            if (app->ui.resizeDrag >= 0) {
                const float m = (app->ui.resizeDrag == 1 || app->ui.resizeDrag == 2) ? x : y;
                const int64_t delta = static_cast<int64_t>(m - app->ui.resizeStartMouse);
 // 统一拖拽方向（Windows 缩放直觉——边缘朝哪拖面板就往哪扩展）：
 // 顶栏(0)下缘向下拖变高、左栏(1)右缘向右拖变宽（+）；右栏(2)左缘向左拖变宽、
 // 底栏(3)上缘向上拖变高 → 与左/顶相反，取反
                const int64_t deltaSigned = (app->ui.resizeDrag == 2 || app->ui.resizeDrag == 3) ? -delta : delta;
                uint32_t* val = (app->ui.resizeDrag == 0) ? &app->ui.panelTopH :
                                (app->ui.resizeDrag == 1) ? &app->ui.panelLeftW :
                                (app->ui.resizeDrag == 2) ? &app->ui.panelRightW : &app->ui.panelBottomH;
                const uint32_t minVal = (app->ui.resizeDrag == 0) ? kTopBarHeight :
                                        (app->ui.resizeDrag == 1) ? kLeftBarWidth :
                                        (app->ui.resizeDrag == 2) ? kSideBarWidth : kBottomBarHeight;
                const uint32_t W = app->vk.swapchainExtent.width;
                const uint32_t H = app->vk.swapchainExtent.height;
 constexpr uint32_t kViewportMin = 60; // 视口最小保留
                uint32_t maxVal = minVal;
                if (app->ui.resizeDrag == 0)      maxVal = (H > app->ui.panelBottomH + kViewportMin) ? H - app->ui.panelBottomH - kViewportMin : minVal;
                else if (app->ui.resizeDrag == 3) maxVal = (H > app->ui.panelTopH + kViewportMin)    ? H - app->ui.panelTopH    - kViewportMin : minVal;
                else if (app->ui.resizeDrag == 1) maxVal = (W > app->ui.panelRightW + kViewportMin)  ? W - app->ui.panelRightW  - kViewportMin : minVal;
                else                           maxVal = (W > app->ui.panelLeftW + kViewportMin)   ? W - app->ui.panelLeftW   - kViewportMin : minVal;
                int64_t nv = static_cast<int64_t>(app->ui.resizeStartVal) + deltaSigned;
                nv = std::max<int64_t>(minVal, std::min<int64_t>(nv, maxVal));
                *val = static_cast<uint32_t>(nv);
 return 0; // 拖拽分隔线期间屏蔽其它交互
            }
            const float dx = x - app->camera.lastX;
            const float dy = y - app->camera.lastY;
 // 旋转拖拽（gizmoDragMode==4）——沿环切向：鼠标绕枢轴屏幕投影的角度差（顺时针拖=正转，径向拖不转）
            if (app->gizmo.gizmoDragging && app->gizmo.gizmoDragMode == 4 &&
                app->scene.selectedObject >= 0 &&
                app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                SceneObject& so = app->scene.objects[app->scene.selectedObject];
 if (app->gizmo.gizmoAxis == -2) { // /355：中心 trackball——水平拖绕世界Y、垂直拖绕世界X（世界空间，朝向不影响方向）
                    float R0[16], Ry[16], Rx[16], R1[16], Rnew[16];
                    BuildRotFromEuler(app->gizmo.gizmoStartRx, app->gizmo.gizmoStartRy, app->gizmo.gizmoStartRz, R0);
                    MakeWorldRot(1, -dx * 0.35f, Ry);
                    MakeWorldRot(0, -dy * 0.35f, Rx);
                    MatMul4(Ry, R0, R1);
                    MatMul4(Rx, R1, Rnew);
                    EulerFromR(Rnew, so.rx, so.ry, so.rz);
                    if (dx * dx + dy * dy > 4.0f) app->scene.mouseDragged = true;
                } else {
                    const float a0 = std::atan2(app->scene.pressY - app->gizmo.gizmoScreenPivotY,
                                                app->scene.pressX - app->gizmo.gizmoScreenPivotX);
                    const float a1 = std::atan2(y - app->gizmo.gizmoScreenPivotY,
                                                x - app->gizmo.gizmoScreenPivotX);
                    float dTheta = (a1 - a0) * 180.0f / 3.14159265f;
                    while (dTheta > 180.0f) dTheta -= 360.0f;
                    while (dTheta < -180.0f) dTheta += 360.0f;
 // 旋转方向随视角修正——环法线朝向相机时屏幕角与右手旋转同向、背离时反向；
 // 固定符号会让背离相机的轴（默认即蓝轴 Z）旋转反向。s>=0 维持原 -dTheta，s<0 翻转。
                    float view[16];
                    app->camera.ViewMatrix(view);
 const float toViewer[3] = {view[2], view[6], view[10]}; // 场景→相机方向（世界）
                    const float dirs3[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                    const float s = dirs3[app->gizmo.gizmoAxis][0] * toViewer[0]
                                  + dirs3[app->gizmo.gizmoAxis][1] * toViewer[1]
                                  + dirs3[app->gizmo.gizmoAxis][2] * toViewer[2];
                    const float sign = (s >= 0.0f) ? 1.0f : -1.0f;
 // 世界空间旋转——绕世界轴 premultiply，gizmo 固定不随物体旋转，朝向不影响拖拽方向
                    float R0[16], Rw[16], Rnew[16];
                    BuildRotFromEuler(app->gizmo.gizmoStartRx, app->gizmo.gizmoStartRy, app->gizmo.gizmoStartRz, R0);
                    MakeWorldRot(app->gizmo.gizmoAxis, -dTheta * sign, Rw);
                    MatMul4(Rw, R0, Rnew);
                    EulerFromR(Rnew, so.rx, so.ry, so.rz);
                    if (dTheta * dTheta > 4.0f) app->scene.mouseDragged = true;
                }
                app->camera.lastX = x;
                app->camera.lastY = y;
 return 0; // 屏蔽视角旋转（旋转物体时不可转视角 BUG 修复）
            }
 // 缩放拖拽（gizmoDragMode==5）——沿轴直线：鼠标位移投影到轴屏幕方向（拖轴=单轴，中心=等比）
            if (app->gizmo.gizmoDragging && app->gizmo.gizmoDragMode == 5 &&
                app->scene.selectedObject >= 0 &&
                app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                SceneObject& so = app->scene.objects[app->scene.selectedObject];
                const int axis = app->gizmo.gizmoAxis;
                float f = 1.0f;
 if (axis == 3) { // 中心方块：垂直拖动（上=放大）
                    f = 1.0f + (app->scene.pressY - y) * 0.004f;
                } else if (axis >= 0 && axis <= 2) {
                    const Layout lay = ComputeLayout(*app);
                    const VkRect2D& vp = lay.viewport;
                    float view[16], proj[16], mvp[16];
                    app->camera.ViewMatrix(view);
                    const float aspect = static_cast<float>(vp.extent.width) /
                                         static_cast<float>(vp.extent.height);
                    app->camera.ProjectionMatrix(aspect, proj);
                    MatMul4(proj, view, mvp);
                    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                                         static_cast<float>(vp.extent.width),
                                         static_cast<float>(vp.extent.height), 0.0f, 1.0f};
                    float gp[3];
                    GizmoPivot(so, gp);
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）
                    const float tip[3] = {gp[0] + dirs[axis][0], gp[1] + dirs[axis][1],
                                          gp[2] + dirs[axis][2]};
                    float sx0, sy0, sx1, sy1;
                    if (ProjectToViewport(mvp, gvp, gp[0], gp[1], gp[2], sx0, sy0) &&
                        ProjectToViewport(mvp, gvp, tip[0], tip[1], tip[2], sx1, sy1)) {
                        const float ddx = sx1 - sx0, ddy = sy1 - sy0;
                        const float dl = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dl > 1e-6f) {
                            const float move = ((x - app->scene.pressX) * ddx + (y - app->scene.pressY) * ddy) / dl;
                            f = 1.0f + move * 0.004f;
                        }
                    }
                }
                if (f > 0.0f) {
 if (axis == 3) { // 中心方块：等比（三轴同乘，与旋转无关）
                        so.sx = app->gizmo.gizmoStartSx * f; so.sy = app->gizmo.gizmoStartSy * f; so.sz = app->gizmo.gizmoStartSz * f;
                    } else if (axis >= 0 && axis <= 2) {
 // 旋转过（斜着）的物体——世界轴缩放**按公式分配**：
 // 世界轴方向投影到本地坐标 v = R^T·d（R 列 = 本地轴在世界方向），
 // 按 |v| 分量把缩放因子 f 分配到 sx/sy/sz（sx'=sx·(1+(f-1)|vx|)）。
 // 未旋转时 v=(1,0,0) 等 → 退化为单轴，与旧行为一致。
                        float R[16];
                        BuildRotFromEuler(so.rx, so.ry, so.rz, R);
                        const float vx = R[axis], vy = R[4 + axis], vz = R[8 + axis];
                        const float k = f - 1.0f;
                        so.sx = app->gizmo.gizmoStartSx * (1.0f + k * std::fabs(vx));
                        so.sy = app->gizmo.gizmoStartSy * (1.0f + k * std::fabs(vy));
                        so.sz = app->gizmo.gizmoStartSz * (1.0f + k * std::fabs(vz));
                    }
                }
                if (std::fabs(f - 1.0f) > 0.002f) app->scene.mouseDragged = true;
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
 // 中心环自由拖拽射线与"过枢轴的视口平面"求交，每帧增量移动（Blender G 自由方向）
            if (app->gizmo.gizmoDragging && app->gizmo.gizmoDragMode == 3 &&
                app->scene.selectedObject >= 0 &&
                app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                float o[3], d[3];
                if (BuildViewRay(*app, x, y, o, d)) {
                    float view[16];
                    app->camera.ViewMatrix(view);
 const float n[3] = {-view[2], -view[6], -view[10]}; // 相机前向 = 平面法线
                    float cur[3];
                    if (RayPlane(o, d, n, app->gizmo.gizmoPivot, cur)) {
                        if (app->gizmo.gizmoLastHitValid) {
                            SceneObject& so = app->scene.objects[app->scene.selectedObject];
                            so.tx += cur[0] - app->gizmo.gizmoLastHit[0];
                            so.ty += cur[1] - app->gizmo.gizmoLastHit[1];
                            so.tz += cur[2] - app->gizmo.gizmoLastHit[2];
                            if (dx * dx + dy * dy > 25.0f) app->scene.mouseDragged = true;
                        }
                        app->gizmo.gizmoLastHit[0] = cur[0];
                        app->gizmo.gizmoLastHit[1] = cur[1];
                        app->gizmo.gizmoLastHit[2] = cur[2];
                        app->gizmo.gizmoLastHitValid = true;
                    }
                }
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
 // 移动三向标拖拽：物体沿锁定轴移动（射线-轴最近点参数 t 的增量）
            if (app->gizmo.gizmoDragging && app->gizmo.gizmoAxis >= 0 &&
                app->scene.selectedObject >= 0 &&
                app->scene.selectedObject < static_cast<int>(app->scene.objects.size())) {
                SceneObject& so = app->scene.objects[app->scene.selectedObject];
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）
                const float* dir = dirs[app->gizmo.gizmoAxis];
                float o[3], d[3];
                float t = app->gizmo.gizmoStartT;
                if (BuildViewRay(*app, x, y, o, d)) ClosestAxisParam(app->gizmo.gizmoPivot, dir, o, d, t);
                const float delta = t - app->gizmo.gizmoStartT;
                so.tx = app->gizmo.gizmoStartTx + dir[0] * delta;
                so.ty = app->gizmo.gizmoStartTy + dir[1] * delta;
                so.tz = app->gizmo.gizmoStartTz + dir[2] * delta;
                if (dx * dx + dy * dy > 25.0f) app->scene.mouseDragged = true;
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
 // 框选拖拽更新（左键框选进行中，更新矩形当前角；位移>4px 视为拖拽非点击）
            if (app->ui.marqueeSelecting) {
                app->ui.marqueeX1 = x;
                app->ui.marqueeY1 = y;
                const float mx0 = x - app->ui.marqueeX0, my0 = y - app->ui.marqueeY0;
                if (mx0 * mx0 + my0 * my0 > 16.0f) app->scene.mouseDragged = true;
            }
 // 左键按住且移动>4px → 进入 orbit（纯点击左键走下方拾取）
            if (app->ui.lbuttonDown && !app->camera.orbiting) {
                const float mdx = x - app->camera.lastX, mdy = y - app->camera.lastY;
                if (mdx * mdx + mdy * mdy > 16.0f) app->camera.orbiting = true;
            }
            if (app->camera.orbiting) {
 // 视角拖动状态机——一旦进入拖动即视为拖拽（含慢速拖动），
 // 松开时不再触发物体拾取/取消选择（鼠标划过物体也不影响）
                app->scene.mouseDragged = true;
 // 旋转视角 → 显示左下角万向球
                app->ui.navLastActionMs = GetTickCount64();
                app->ui.navLastActionType = 1;
                app->camera.Orbit(dx, dy);
            } else if (app->camera.panning) {
                app->camera.Pan(dx, dy, static_cast<float>(app->vk.swapchainExtent.height));
 // 渐隐中心由 DrawFrame 每帧按摄像机位置更新，这里无需单独处理
            }
            app->camera.lastX = x;
            app->camera.lastY = y;

            for (auto& b : app->ui.buttons) {
                b.machine.OnMouseMove(PointInButton(b, x, y));
            }
            app->ui.appIcon.machine.OnMouseMove(PointInButton(app->ui.appIcon, x, y));
            for (int i = 0; i < 3; ++i)
                app->ui.sysButtons[i].machine.OnMouseMove(PointInButton(app->ui.sysButtons[i], x, y));
            for (auto& b : app->ui.ballButtons) {
                b.machine.OnMouseMove(PointInButton(b, x, y));
            }
            if (app->ui.menuOpen) {
                for (int i = 0; i < 2; ++i) {
                    app->ui.menuItems[i].machine.OnMouseMove(PointInButton(app->ui.menuItems[i], x, y));
                }
            }
        }
        return 0;
    case WM_NCHITTEST: {
        if (App* app = GetApp(hwnd)) {
            // 屏幕坐标 → 客户区坐标（ScreenToClient 与按钮 rect / swapchainExtent 同一坐标空间）。
            // 原 GetWindowRect 近似在 DPI/多屏下错位 → 控件命中全失败、整条顶栏只会拖动。改用 ScreenToClient 修复。
            POINT sp{ static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
                      static_cast<LONG>(static_cast<short>(HIWORD(lParam))) };
            platform::ScreenToClient(g_mainWindow, &sp);
            const float cx = static_cast<float>(sp.x);
            const float cy = static_cast<float>(sp.y);
            const int W = static_cast<int>(app->vk.swapchainExtent.width);
            const int H = static_cast<int>(app->vk.swapchainExtent.height);
            const int topH = static_cast<int>(app->ui.panelTopH);
            constexpr float kCaptionDragH = 9.0f;   // 窗口移动检查高度：顶栏 36px 缩小 4 倍 → 9px

            // 顶栏：命中控件 → 走客户区（按钮可点击）；空白最上 9px → 标题栏拖拽（HTCAPTION）
            // 【诊断探针】hitCode：0=HTCAPTION 1=HTLEFT 2=HTRIGHT 3=HTBOTTOM 4=HTBOTTOMLEFT 5=HTBOTTOMRIGHT
            int hitCode = -1;
            if (cy >= 0.0f && cy < static_cast<float>(topH)) {
                bool onCtrl = PointInButton(app->ui.appIcon, cx, cy);
                for (auto& b : app->ui.buttons) if (PointInButton(b, cx, cy)) onCtrl = true;
                for (int i = 0; i < 3 && !onCtrl; ++i)
                    if (PointInButton(app->ui.sysButtons[i], cx, cy)) onCtrl = true;
                if (!onCtrl && cy < kCaptionDragH) hitCode = 0;
                // 命中控件 / 拖拽区之下的顶栏空白：落到下方 → HTCLIENT（按钮正常响应）
            } else if (W > 0 && H > 0) {
                // 无边框自由缩放（左/右/底边 6px + 底边两角）。
                // 最大化状态也返回缩放代码：WM_NCLBUTTONDOWN 会先还原 normalRect 再缩放
                // （否则启动默认最大化 → 边缘永远 HTCLIENT → "窗口无法缩放"）。
                const int bz = 6;
                const bool left   = cx <  bz;
                const bool right  = cx >  W - bz;
                const bool bottom = cy >  H - bz;
                if (bottom && left)       hitCode = 4;
                else if (bottom && right) hitCode = 5;
                else if (left)            hitCode = 1;
                else if (right)           hitCode = 2;
                else if (bottom)          hitCode = 3;
            }
            if (hitCode >= 0) {
                static int dbgHit = 0;
                if (dbgHit++ < 20)
                    VkbLog(("[dbg] NCHITTEST -> " + std::to_string(hitCode) + " cx=" + std::to_string((int)cx) +
                            " cy=" + std::to_string((int)cy) + " max=" + (app->ui.maximized ? "1" : "0")).c_str());
                static const LRESULT kCodes[] = { (LRESULT)HTCAPTION, (LRESULT)HTLEFT, (LRESULT)HTRIGHT,
                                                  (LRESULT)HTBOTTOM,  (LRESULT)HTBOTTOMLEFT, (LRESULT)HTBOTTOMRIGHT };
                return kCodes[hitCode];
            }
        }
        // 其余区域统一返回 HTCLIENT（客户区，允许 orbit / 物体拾取）。
        // 注：原代码此处 break 落到 DefWindowProc(WM_NCHITTEST)；本机（报 Windows 6.0 + 真实 NVIDIA 1.4 驱动）
        // 下 DefWindowProc 处理部分窗口消息会永久阻塞主线程（同 49357 私有消息卡死），故不再交 DefWindowProc，
        // 自绘无边框窗口客户区返回 HTCLIENT 与之等价，可规避该隐患。
        return (LRESULT)HTCLIENT;
    }
    case WM_NCLBUTTONDOWN:
        // 自定义标题栏拖拽 / 边缘缩放：不交给 DefWindowProc（本机 DefWindowProc 原生模态
        // 拖拽/缩放循环会卡死/崩溃，与之前 WM_NCHITTEST 不交 DefWindowProc 同因）。
        // 改为 SetCapture 后在 WM_MOUSEMOVE 里自行 SetWindowPos，释放时在 WM_LBUTTONUP 做边缘吸附。
        if (App* app = GetApp(hwnd)) {
            if (wParam == HTCAPTION) {
                app->ui.captionDragging = true;
                app->ui.edgeResizing   = false;
                app->ui.dragStartX = static_cast<int>(LOWORD(lParam));
                app->ui.dragStartY = static_cast<int>(HIWORD(lParam));
                platform::GetWindowRect(g_mainWindow, &app->ui.dragOrigRect);
                // 最大化下延后还原：移动 ≥25px 才视为拖动退出最大化（<25px 视为单击 → 不还原）
                if (app->ui.maximized) {
                    app->ui.pendingRestore = true;
                    platform::SetCapture(g_mainWindow);
                    return 0;
                }
                platform::SetCapture(g_mainWindow);
                return 0;
            }
            int edge = 0;
            if      (wParam == HTLEFT)        edge = 1;
            else if (wParam == HTRIGHT)       edge = 2;
            else if (wParam == HTBOTTOM)      edge = 3;
            else if (wParam == HTBOTTOMLEFT)  edge = 4;
            else if (wParam == HTBOTTOMRIGHT) edge = 5;
            if (edge) {
                static int dbgNclb = 0;
                if (dbgNclb++ < 10)
                    VkbLog(("[dbg] NCLB edge=" + std::to_string(edge) + " max=" + (app->ui.maximized ? "1" : "0")).c_str());
                app->ui.edgeResizing     = true;
                app->ui.resizeEdge       = edge;
                app->ui.captionDragging = false;
                app->ui.dragStartX = static_cast<int>(LOWORD(lParam));
                app->ui.dragStartY = static_cast<int>(HIWORD(lParam));
                platform::GetWindowRect(g_mainWindow, &app->ui.dragOrigRect);
                // 若当前已最大化：先还原到 normalRect，再以还原后位置为基准缩放
                // （最大化时窗口铺满工作区，不还原无法改变尺寸）
                if (app->ui.maximized) {
                    app->ui.maximized = false;
                    platform::SetWindowPos(g_mainWindow, nullptr,
                        app->ui.normalRect.left, app->ui.normalRect.top,
                        app->ui.normalRect.right - app->ui.normalRect.left,
                        app->ui.normalRect.bottom - app->ui.normalRect.top,
                        SWP_NOZORDER | SWP_FRAMECHANGED);
                    platform::GetWindowRect(g_mainWindow, &app->ui.dragOrigRect);
                    app->ui.dragStartX = static_cast<int>(LOWORD(lParam));
                    app->ui.dragStartY = static_cast<int>(HIWORD(lParam));
                }
                platform::SetCapture(g_mainWindow);
                return 0;
            }
        }
        break;  // 其他 NC 消息仍交 DefWindowProc
    case WM_EXITSIZEMOVE:
        // 标题栏拖拽结束（释放鼠标）→ Win10 及以下自实现边缘吸附；Win11+ 由系统原生 Snap 处理
        if (App* app = GetApp(hwnd)) {
            if (app->ui.captionDragging) {
                app->ui.captionDragging = false;
                if (!platform::IsWindows11OrLater()) {
                    RECT wr{}; platform::GetWindowRect(g_mainWindow, &wr);
                    RECT mw{}, wk{};
                    platform::GetWindowMonitorRect(g_mainWindow, &mw, &wk);
                    const int snap = 12;
                    if (wr.top <= mw.top + snap) {
                        platform::GetWindowRect(g_mainWindow, &app->ui.normalRect);  // 记住吸附前位置，供还原
                        app->ui.maximized = true;
                        // 用工作区显式定位（不用 SW_MAXIMIZE：对 WS_POPUP 窗口可能铺满整屏遮住任务栏）
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.left, wk.top,
                                     wk.right - wk.left, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    } else if (wr.left <= mw.left + snap) {
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.left, wk.top,
                                     (wk.right - wk.left) / 2, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    } else if (wr.right >= mw.right - snap) {
                        const int hw = (wk.right - wk.left) / 2;
                        platform::SetWindowPos(g_mainWindow, nullptr, wk.right - hw, wk.top,
                                     hw, wk.bottom - wk.top,
                                     SWP_NOZORDER | SWP_FRAMECHANGED);
                    }
                }
            }
        }
        return 0;
    case WM_APP:
        if (App* appPtr = GetApp(hwnd)) ApplyImportResult(*appPtr);
        return 0;
    case WM_APP + 2:
 // 我的世界异步加载完成：主线程收尾（存入 3D 模型存储区 + 启动 2D 投射线程）
        if (GetApp(hwnd)) {
            auto& mc = McWorldImporter::Instance();
            if (wParam == 1 && mc.GetBuiltReady()) {
 // 取走后台线程构建结果，存入 3D 模型存储区（不直接渲染到视口）
                SceneObject obj; McBlockGrid grid; McAtlas atlas;
                mc.TakeBuilt(obj, grid, atlas);
                StoredModel sm;
                sm.obj = std::move(obj);
                sm.label = mc.GetVersion();
                g_mcModelStore.emplace_back(std::move(sm));
                g_mcProgress = 100;
                mc.SetLoading(false);
                if (HWND mcHwnd = mc.GetHwnd()) {
                    InvalidateRect(mcHwnd, nullptr, FALSE);
 // 确保地图窗口保持前台（防止加载完成瞬间后台软件被激活蹦出来）
                    SetForegroundWindow(mcHwnd);
                }
            } else {
 // 验证/构建失败：关闭 loading 窗口 + 弹报错（保持 owner 可用）
                std::wstring reason = mc.GetLastReason();
                mc.SetLoading(false);
                mc.CloseWindow();
                MessageBoxW(hwnd, reason.c_str(), L"awa - 我的世界导入", MB_OK | MB_ICONERROR);
                g_mcProgress = -1;
            }
        }
        return 0;
    default:
        // 修复：已注册窗口消息（0xC000+，如系统/第三方私有的 TaskbarButtonCreated 等）
        // 本就不该交给 DefWindowProc 处理；本机（报 Windows 6.0 + 真实 NVIDIA 1.4 驱动）下
        // DefWindowProc 对其会阻塞主线程导致假死。直接返回 0 忽略，任务栏图标由窗口样式+
        // WM_SETICON 决定，与此消息无关。
        if (msg >= 0xC000) return 0;
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// 主窗口 platform:: 句柄（P1：窗口创建收口到 platform::，供退出时销毁）
platform::Window* g_mainWindow = nullptr;

bool CreateWindowApp(App& app, HINSTANCE hInstance) {
    const wchar_t kClassName[] = L"VulkanBlankWindow";
    // 还原尺寸/最大化状态为 app 布局逻辑，留在本函数
    RECT mr{};
    platform::GetPrimaryMonitorRect(nullptr, &mr);  // 工作区（避开任务栏，与窗口创建/最大化一致）
    app.ui.maximized = true;  // 默认铺满工作区（非整屏，任务栏保持可见可点）
    // 还原用窗口尺寸（居中、约 1200x780，避开任务栏留白）
    const LONG nW = std::min<LONG>(1200, mr.right - mr.left - 80);
    const LONG nH = std::min<LONG>(780, mr.bottom - mr.top - 80);
    app.ui.normalRect = {
        (mr.right - mr.left - nW) / 2 + mr.left,
        (mr.bottom - mr.top - nH) / 2 + mr.top,
        (mr.right - mr.left - nW) / 2 + mr.left + nW,
        (mr.bottom - mr.top - nH) / 2 + mr.top + nH};

    // OS 边界收口：窗口创建委托 platform::（未来 macOS/Linux 仅替换后端）
    g_mainWindow = platform::CreateMainWindow(hInstance, kClassName, kWindowTitle, WndProc, &app);
    if (!g_mainWindow) return false;
    app.hwnd = g_mainWindow->hwnd;  // 兼容既有 app.hwnd 用法（NCHITTEST/SetWindowPos/GetApp）
    return true;
}

// 全屏/还原切换：maximized=true 时铺满【工作区】，false 时恢复 normalRect 窗口。
// 顶栏空白拖拽（WM_NCHITTEST 返回 HTCAPTION）依赖此状态判断。
// 注意：必须用工作区 rcWork 而非整屏 rcMonitor——否则最大化会遮住任务栏，
// 用户无法切换窗口/只能按 Win 键脱离（与 CreateMainWindow 的修复保持一致）。
static void ToggleMaximize(App& app) {
    RECT mr{};
    platform::GetWindowMonitorRect(g_mainWindow, nullptr, &mr);  // 取工作区（避开任务栏）
    if (app.ui.maximized) {
        app.ui.maximized = false;
        platform::SetWindowPos(g_mainWindow, nullptr,
                     app.ui.normalRect.left, app.ui.normalRect.top,
                     app.ui.normalRect.right - app.ui.normalRect.left,
                     app.ui.normalRect.bottom - app.ui.normalRect.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
    } else {
        platform::GetWindowRect(g_mainWindow, &app.ui.normalRect);  // 记住当前窗口位置供下次还原
        app.ui.maximized = true;
        platform::SetWindowPos(g_mainWindow, nullptr, mr.left, mr.top,
                     mr.right - mr.left, mr.bottom - mr.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

// ---------------------------------------------------------------------------




// ---------------------------------------------------------------------------













// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------




// ==================== 3D 视口比例尺左下角，表示当前摄像机缩放大小====================

// 摄像机坐标显示平移时左下角显示 X/Y/Z 三行坐标（3 位精度）。

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

// 物体显示栏文字标签纹理上传多行列表复用；参数直接给 RGBA。


// ============================ 缩放距离条 + 摄像机坐标 淡入淡出 ============================


// 依赖 DLL 检测（用户要求）：软件依赖的 .dll 从当前根目录（exe 所在目录）加载，
// 缺失时明确报错退出，而不是静默失败或依赖系统目录兜底
// 获取真实 Windows 版本用 RtlGetVersion 动态加载，避免 GetVersionEx 在
bool GetWindowsVersion(int& major, int& minor, int& build) {
 // RTL_OSVERSIONINFOW 布局（与 OSVERSIONINFOW 相同，但可独立声明避免依赖版本头）
    struct OsVer {
        ULONG size;
        ULONG majorVer;
        ULONG minorVer;
        ULONG buildNum;
        ULONG platformId;
        WCHAR csd[128];
    };
    typedef LONG(WINAPI * RtlGetVersionFn)(OsVer*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!fn) return false;
    OsVer ov{};
    ov.size = sizeof(ov);
    if (fn(&ov) != 0) return false;
    major = static_cast<int>(ov.majorVer);
    minor = static_cast<int>(ov.minorVer);
    build = static_cast<int>(ov.buildNum);
    return true;
}

// Windows 7 窗口兼容：主窗口/设置窗口均使用标准 WS_OVERLAPPEDWINDOW + 系统标题栏，
bool CheckRuntimeDlls() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash + 1);

    std::vector<std::wstring> missing;
    const char* names = VKB_NEEDED_DLLS;
    while (names && *names) {
        while (*names == ' ') ++names;
        if (!*names) break;
        const char* sp = std::strchr(names, ' ');
        const std::string name = sp ? std::string(names, sp - names) : std::string(names);
        std::wstring wname(name.begin(), name.end());
        if (GetFileAttributesW((dir + wname).c_str()) == INVALID_FILE_ATTRIBUTES) {
            missing.push_back(wname);
        }
        if (!sp) break;
        names = sp + 1;
    }
    if (!missing.empty()) {
        std::wstring msg = L"缺少运行所需 DLL，请将下列文件放置到程序目录后重试：\n\n";
        for (const auto& m : missing) msg += L"  " + m + L"\n";
        MessageBoxW(nullptr, msg.c_str(), L"awa - 缺少 DLL", MB_ICONERROR | MB_OK);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 进程崩溃而非优雅失败——此兜底保证下次崩溃一定有日志和弹窗信息
// ---------------------------------------------------------------------------
// 进程崩溃而非优雅失败——此兜底保证下次崩溃一定有日志和弹窗信息
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    char moduleName[MAX_PATH] = "未知模块";
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(ep->ExceptionRecord->ExceptionAddress),
                           &hMod) && hMod) {
        GetModuleFileNameA(hMod, moduleName, MAX_PATH);
    }
    const char* base = moduleName;
    for (const char* p = moduleName; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;

    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "[crash] 异常 0x%08lX @ 0x%p 模块=%s 阶段=%s（%s）",
                  static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                  ep->ExceptionRecord->ExceptionAddress, base, g_stage,
                  ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                      ? "内存访问违规" : "其他");
    VkbLog(buf);
    MessageBoxA(nullptr, buf, "awa - 崩溃", MB_ICONERROR | MB_OK);
    return EXCEPTION_CONTINUE_SEARCH;
}

// 鼠标灵敏度（0~100）↔ orbit/pan 输入增益因子 现定义于 awa_internal.h（SensToFactor / FactorToSens 内联）。



// 设置窗口现改为事件驱动：滑动条/选项变更时由 OpenSettingsWindow 持有的 App& 直接写回并保存，
// 不再经外部全局 + 主循环每帧轮询（SensToFactor 已移至 awa_internal.h 内联）。

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
 // DPI 感知——Windows 缩放下 swapchain 用物理像素，2D 不再被系统放大模糊
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFn)(HANDLE);
        const auto dpiCtx = hUser32 ? reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext")) : nullptr;
        if (dpiCtx) {
 dpiCtx(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (Win10 1803+)
        } else {
 SetProcessDPIAware(); // Win7/Win8 回退（系统级 DPI 感知）
        }
    }

 // 单实例检查——已有相同程序运行时弹窗退出，
 // 避免双实例同时初始化 Vulkan/交换链导致双双崩溃
    {
        HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"awa_singleton_vulkan_viewer");
        if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(hMutex);
            MessageBoxW(nullptr, L"已有相同软件运行！", L"awa - 单实例检查",
                        MB_OK | MB_ICONERROR);
            return 0;
        }
 // 正常路径：不关闭互斥体句柄（随进程退出由系统释放，否则第二实例可再启动）
    }

    App app;

 // 崩溃兜底：任何未处理异常 → 写 awa.log + 弹窗（核显/虚拟机兼容排查的关键手段）
    SetUnhandledExceptionFilter(CrashHandler);

 // 用 GetCommandLineW（UTF-16，中文路径安全）；取第一个带引号参数（若有）
    {
        const wchar_t* cmd = GetCommandLineW();
        wchar_t* arg1 = nullptr;
 // 与 -fshort-wchar 的 2 字节 UTF-16 错位 → 必须自己按 wchar_t 步进遍历
        auto findQuote = [](const wchar_t* s, wchar_t ch) -> const wchar_t* {
            if (!s) return nullptr;
            for (; *s; ++s) if (*s == ch) return s;
            return nullptr;
        };
        const wchar_t* q = findQuote(cmd, L'"');
        if (q) {
            q++;
            const wchar_t* q2 = findQuote(q, L'"');
            if (q2) {
                q = q2 + 1;
                while (*q == L' ' || *q == L'\t') ++q;
                if (*q == L'"') {
                    q++;
                    const wchar_t* e = findQuote(q, L'"');
                    if (e) {
                        const size_t n = static_cast<size_t>(e - q);
                        if (n > 0 && n < MAX_PATH) {
                            for (size_t i = 0; i < n; ++i) g_startupObjPath[i] = q[i];
                            g_startupObjPath[n] = L'\0';
                            arg1 = g_startupObjPath;
                        }
                    }
                }
            }
        }
        if (arg1) {
            VkbLog("[startup] 命令行指定模型，启动时直接加载");
        }
    }

 // 记录真实系统版本（Win7 兼容窗口：标准 WS_OVERLAPPEDWINDOW 全版本通用，此处仅日志）
    {
        int wmaj = 0, wmin = 0, wbuild = 0;
        if (GetWindowsVersion(wmaj, wmin, wbuild)) {
            VkbLog(("[os] Windows " + std::to_string(wmaj) + "." + std::to_string(wmin) +
                    " build " + std::to_string(wbuild)).c_str());
        }
    }

    if (!LoadVulkanCore()) {
        ShowErrorBox(g_loaderError ? g_loaderError : "Vulkan 加载器初始化失败");
        return 1;
    }

    if (!CheckRuntimeDlls()) return 1;

 // 抗锯齿默认：核显（集成 GPU）默认**无抗锯齿**（性能优先，）；
 // 独立显卡默认 FXAA。保存过设置/环境变量时以用户选择为准
    app.aa.aaMode = (app.vk.gpuType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? AAMode::None : AAMode::FXAA;
    const int savedAa = LoadSettingInt("aa_mode", -1);
    if (savedAa >= 0 && savedAa <= 4) {
        app.aa.aaMode = static_cast<AAMode>(savedAa);
    } else if (const char* env = std::getenv("AWA_AA"); env && env[0] >= '0' && env[0] <= '4' && env[1] == 0) {
        app.aa.aaMode = static_cast<AAMode>(env[0] - '0');
    }
    app.aa.msaaEnabled = false;
    app.aa.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    switch (app.aa.aaMode) {
    case AAMode::MSAA_2x: app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_2_BIT; break;
    case AAMode::MSAA_4x: app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    case AAMode::SSAA:    app.aa.msaaEnabled = true; app.aa.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    default: break;
    }
 // 摄像机阻尼设置项：从 awa_settings.txt 加载（0~100，默认 85）
    const int savedDamp = LoadSettingInt("camera_damping", -1);
    if (savedDamp >= 0 && savedDamp <= 100) {
        app.camera.damping = savedDamp / 100.0f;
    }

 // 鼠标滑动灵敏度设置项：从 awa_settings.txt 加载（0~100，默认 50）
    const int savedSens = LoadSettingInt("mouse_sensitivity", -1);
    if (savedSens >= 0 && savedSens <= 100) {
        app.camera.orbitSensitivity = SensToFactor(savedSens);
    } else {
        app.camera.orbitSensitivity = SensToFactor(50);
    }

 // 面板尺寸持久化——顶栏固定默认高度（不读 panel_top_h，取消顶栏缩放）；左/右/底可拖
    app.ui.panelTopH = kTopBarHeight;
    {
        // 左栏固定 kLeftBarWidth（不可缩放），不读 panel_left_w 设置
        const int rr = LoadSettingInt("panel_right_w", -1);
        if (rr >= static_cast<int>(kSideBarWidth))   app.ui.panelRightW = static_cast<uint32_t>(rr);
        const int b = LoadSettingInt("panel_bottom_h", -1);
        if (b >= static_cast<int>(kBottomBarHeight)) app.ui.panelBottomH = static_cast<uint32_t>(b);
    }

 // 初始化失败统一处理：先写日志（exe 同目录 awa.log 定位具体失败点）再弹窗
    auto failExit = [&](const char* stage, bool withCleanup) {
        VkbLog((std::string("[init] 失败@") + stage + ": " + g_error).c_str());
        ShowErrorBox(g_error.c_str());
        if (withCleanup) Cleanup(app);
        return 1;
    };

    if (!CreateWindowApp(app, hInstance)) return failExit("CreateWindowApp", false);
    VkbLog("[init] OK CreateWindowApp");
    if (!CreateInstance(app)) {
 // vkCreateInstance 失败最常见原因：系统未安装 Vulkan 驱动（ICD）
        std::string msg = g_error;
        if (g_error.find("vkCreateInstance") != std::string::npos) {
            msg += "\n\n系统未安装 Vulkan 驱动（ICD），无法创建 Vulkan 实例。";
            msg += "\n• 物理机：请更新显卡驱动。";
            msg += "\n• 虚拟机：请安装软件 Vulkan ICD（如 Mesa Lavapipe / SwiftShader），或开启 GPU 3D 加速。";
        }
        VkbLog(("[init] 失败@CreateInstance: " + msg).c_str());
        ShowErrorBox(msg.c_str());
        Cleanup(app);
        return 1;
    }
    if (!CreateSurface(app, hInstance) || !PickPhysicalDevice(app) || !CreateDevice(app) ||
        !CreateSwapchain(app, VK_NULL_HANDLE)) {
        return failExit("CreateSurface/PickPhysicalDevice/CreateDevice/CreateSwapchain", true);
    }
    VkbLog("[init] OK CreateSurface/PickPhysicalDevice/CreateDevice/CreateSwapchain");
    if (!CreateMSAAColorResources(app)) return failExit("CreateMSAAColorResources", true);
    VkbLog("[init] OK CreateMSAAColorResources");
    if (!CreateFXAAResources(app)) return failExit("CreateFXAAResources", true);
    VkbLog("[init] OK CreateFXAAResources");
    if (!CreateDepthResources(app)) return failExit("CreateDepthResources", true);
    VkbLog("[init] OK CreateDepthResources");
 // 兼容路径（老显卡/核显不支持动态渲染）需创建 render pass + framebuffer
    if (!app.vk.useDynamicRendering && (!CreateRenderPass(app) || !CreateFramebuffers(app))) {
        return failExit("CreateRenderPass/CreateFramebuffers", true);
    }
    VkbLog("[init] OK RenderPass/Framebuffers");
    LoadSceneObjects(app);

 // 任一步失败写详细日志（exe 同目录 awa.log 定位具体失败点，核显兼容排查用）
    bool initOk = true;
    auto initStep = [&](bool ok, const char* name) {
        VkbLog((std::string("[init] ") + (ok ? "OK " : "失败 ") + name).c_str());
        if (!ok) {
            initOk = false;
            VkbLog((std::string("[init] 失败@") + name + ": " + g_error).c_str());
        }
        return ok;
    };
    initStep(CreateVertexBuffer(app), "CreateVertexBuffer");
    initStep(CreatePipeline(app), "CreatePipeline");
 // 关键：UI 面板管线（pipelineUI）默认 1 采样，必须启动时创建。
 // 之前只在 ApplyAAMode 内创建，导致启动首帧 UI pass 绑定空管线 → UI 不显示/崩溃。
    initStep(CreatePipelineUI(app), "CreatePipelineUI");
    initStep(CreateMenuPipeline(app), "CreateMenuPipeline");
    initStep(CreatePipelineGrid(app), "CreatePipelineGrid");
    initStep(CreatePipelineSolid(app), "CreatePipelineSolid");
    initStep(CreateVertexBuffer3D(app), "CreateVertexBuffer3D");
    initStep(CreatePipelineAxis(app), "CreatePipelineAxis");
    initStep(CreatePipelineLine3d(app), "CreatePipelineLine3d");
    initStep(CreatePipelineFXAA(app), "CreatePipelineFXAA");
    initStep(CreatePipelineOutline(app), "CreatePipelineOutline");
    initStep(CreateCommandResources(app), "CreateCommandResources");
    if (!initOk) {
        ShowErrorBox(g_error.c_str());
        Cleanup(app);
        return 1;
    }

 // 注：图标纹理创建失败**降级不退出**核显/老驱动 WIC 或纹理创建异常时保证软件能打开，
 // 仅图标缺失；DrawIcon 对 NULL pipeline/set 有保护
    {
        g_stage = "图标:齿轮(DecodePngWic+CreateTextResources+CreateTextPipeline)";
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kGearPng, kGearPngSize, iconRgba, iw, ih) ||
            !CreateTextResources(app, iconRgba, iw, ih) ||
            !CreateTextPipeline(app)) {
            VkbLog(("[icon] 齿轮图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        }
    }

    {
        g_stage = "图标:笔(CreateIconTexture)";
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kPenPng, kPenPngSize, iconRgba, iw, ih) ||
            !CreateIconTexture(app, iconRgba, iw, ih,
                               app.vk.penImage, app.vk.penMemory, app.vk.penView, app.vk.penDescriptorSet)) {
            VkbLog(("[icon] 笔图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        }
    }

    {
        g_stage = "图标:导入(CreateIconTexture)";
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kImportPng, kImportPngSize, iconRgba, iw, ih) ||
            !CreateIconTexture(app, iconRgba, iw, ih,
                               app.vk.importImage, app.vk.importMemory, app.vk.importView, app.vk.importDescriptorSet)) {
            VkbLog(("[icon] 导入图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        }
    }

    {
        g_stage = "图标:导出(CreateIconTexture)";
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kExportPng, kExportPngSize, iconRgba, iw, ih) ||
            !CreateIconTexture(app, iconRgba, iw, ih,
                               app.vk.exportImage, app.vk.exportMemory, app.vk.exportView, app.vk.exportDescriptorSet)) {
            VkbLog(("[icon] 导出图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        }
    }

    {
        g_stage = "文件图标(kFilePng)";
        // 内嵌资源：用户自制文件夹图标（assets/5.png 原始 94x66），与其他图标一致靠 .inc 嵌入
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kFilePng, kFilePngSize, iconRgba, iw, ih) || iw <= 0 || ih <= 0) {
            VkbLog(("[icon] 文件图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        } else {
            // 等比居中入 square 画布（letterbox），避免非方形图标被拉变形
            const int side = std::max(iw, ih);
            std::vector<uint8_t> sq(static_cast<size_t>(side) * side * 4, 0);
            const int ox = (side - iw) / 2, oy = (side - ih) / 2;
            for (int y = 0; y < ih; ++y)
                for (int x = 0; x < iw; ++x) {
                    const int si = (y * iw + x) * 4;
                    const int di = (((oy + y) * side) + (ox + x)) * 4;
                    sq[di] = iconRgba[si]; sq[di + 1] = iconRgba[si + 1];
                    sq[di + 2] = iconRgba[si + 2]; sq[di + 3] = iconRgba[si + 3];
                }
            if (!CreateIconTexture(app, sq, side, side,
                                   app.vk.fileImage, app.vk.fileMemory, app.vk.fileView, app.vk.fileDescriptorSet)) {
                VkbLog(("[icon] 文件图标纹理创建失败（降级：无图标）: " + g_error).c_str());
                g_error.clear();
            }
        }
    }

    {
        g_stage = "图标:awa logo(CreateIconTexture)";
        std::vector<uint8_t> iconRgba;
        int iw = 0, ih = 0;
        if (!DecodePngWic(kAwaPng, kAwaPngSize, iconRgba, iw, ih) ||
            !CreateIconTexture(app, iconRgba, iw, ih,
                               app.vk.appIconImage, app.vk.appIconMemory,
                               app.vk.appIconView, app.vk.appIconDescriptorSet)) {
            VkbLog(("[icon] awa 图标纹理创建失败（降级：回退齿轮）: " + g_error).c_str());
            g_error.clear();
            // 真回退：awa 加载失败则单独用齿轮 PNG 建一个图标描述符集，
            // 避免 appIconDescriptorSet 为空导致 DrawIcon 啥也不画（图标不可见）。
            if (app.vk.appIconImage  != VK_NULL_HANDLE) { vkDestroyImage(app.vk.device, app.vk.appIconImage,  nullptr); app.vk.appIconImage  = VK_NULL_HANDLE; }
            if (app.vk.appIconMemory != VK_NULL_HANDLE) { vkFreeMemory(app.vk.device, app.vk.appIconMemory, nullptr); app.vk.appIconMemory = VK_NULL_HANDLE; }
            if (app.vk.appIconView   != VK_NULL_HANDLE) { vkDestroyImageView(app.vk.device, app.vk.appIconView, nullptr); app.vk.appIconView = VK_NULL_HANDLE; }
            app.vk.appIconDescriptorSet = VK_NULL_HANDLE;  // 描述符集来自池，不销毁，仅置空待重建
            std::vector<uint8_t> gr{}; int gw = 0, gh = 0;
            if (DecodePngWic(kGearPng, kGearPngSize, gr, gw, gh) &&
                CreateIconTexture(app, gr, gw, gh,
                                 app.vk.appIconImage, app.vk.appIconMemory,
                                 app.vk.appIconView, app.vk.appIconDescriptorSet)) {
                VkbLog("[icon] awa 图标回退齿轮成功");
            }
        }
    }

    {
        g_stage = "球按钮图标(LoadBallIcons)";
        LoadBallIcons(app);
    }

    g_stage = "按钮初始化:LoadButtonTheme";
    app.ui.buttonTheme = LoadButtonTheme();
    {
        UiButton b;
        b.rect = {{8, 4}, {28, 28}};
        b.radius = 4.0f;
        b.icon = 0;
        b.onClick = [](App& a) {
            a.ui.menuOpen = false;
            const int gpuMaj = VK_API_VERSION_MAJOR(a.vk.gpuApiVersion);
            const int gpuMin = VK_API_VERSION_MINOR(a.vk.gpuApiVersion);
            const wchar_t* typeW = L"未知设备";
            switch (a.vk.gpuType) {
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeW = L"核显"; break;
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeW = L"独立显卡"; break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeW = L"虚拟 GPU"; break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeW = L"CPU 软件渲染"; break;
                default: break;
            }
 // 厂商识别（左下角仅显示短码：Intel / AMD / NV / 其他）
            const wchar_t* vendorW = L"GPU";
            switch (a.vk.gpuVendor) {
                case 0x8086: vendorW = L"Intel"; break;
                case 0x1002: case 0x1022: vendorW = L"AMD"; break;
                case 0x10DE: vendorW = L"NV"; break;
                default: break;
            }
            wsprintfW(g_renderBottomText,
                      L"awa %s · 著作人：%s · vulkan %d.%d - %s渲染 · %s",
                      kVersionW, kAuthorW, gpuMaj, gpuMin, typeW, vendorW);
            OpenSettingsWindow(a.hwnd, a);
        };
        for (int i = 0; i < 4; ++i) {
            b.color[i] = app.ui.buttonTheme.normal[i];
            b.border[i] = kBorderColor.float32[i];
        }
        app.ui.buttons.push_back(b);
    }
    {
        UiButton b;
        b.rect = {{12, 4}, {50, 28}};  // 占位 rect，实际位置由 ComputeTopBar 每帧覆盖
        b.radius = 4.0f;
        b.icon = 2;                   // 文件图标（fileDescriptorSet）
        b.onClick = nullptr;          // 占位：点击行为待定
        for (int i = 0; i < 4; ++i) {
            b.color[i] = app.ui.buttonTheme.normal[i];
            b.border[i] = kBorderColor.float32[i];
        }
        app.ui.buttons.push_back(b);
    }
    {
        UiButton b;
        b.rect = {{62, 4}, {50, 28}};
        b.radius = 4.0f;
        b.icon = 1;
        b.onClick = [](App& a) {
            a.ui.menuOpen = !a.ui.menuOpen;
            a.ui.menuAnimStartMs = GetTickCount64();
            a.ui.menuAnimFrom = a.ui.menuAnim;
        };
        for (int i = 0; i < 4; ++i) {
            b.color[i] = app.ui.buttonTheme.normal[i];
            b.border[i] = kBorderColor.float32[i];
        }
        app.ui.buttons.push_back(b);
    }

    if (!app.ui.buttons.empty()) {
        const UiButton& editBtn = app.ui.buttons.back();
        const int btnCenterX = editBtn.rect.offset.x + static_cast<int32_t>(editBtn.rect.extent.width) / 2;
        const int menuX = std::max(btnCenterX - 100, 0);
        app.ui.menuRect = {
            {menuX,
             editBtn.rect.offset.y + static_cast<int32_t>(editBtn.rect.extent.height) + 2},
            {200, 0}
        };
    }

 // 第二条分割线右侧新建 3 个相连按钮（各宽 50px、无间隔）
 // 绑定变换模式——按钮1=移动 按钮2=旋转 按钮3=缩放（gizmoMode 0/1/2）
 // 注：rect 由 ComputeTopBar 每帧覆盖（顺位布局），此处仅给初始占位值。
    {
        for (int i = 0; i < 3; ++i) {
            UiButton b;
            b.rect = {{12 + i * 50, 4}, {50, 28}};
            b.radius = 4.0f;
            b.icon = 0;
 b.onClick = nullptr; // 变换模式在 WM_LBUTTONUP 按按钮索引设置（onClick 是函数指针不能捕获）
            for (int c = 0; c < 4; ++c) {
                b.color[c] = app.ui.buttonTheme.normal[c];
                b.border[c] = kBorderColor.float32[c];
            }
            app.ui.buttons.push_back(b);
        }
    }

    // ---- 顶栏宽按钮文字（设置/文件/编辑）：默认中文，button_labels.txt（exe 同目录）可外部覆盖 ----
    if (app.ui.buttons.size() >= 3) {
        static const wchar_t* kDefLabels[3] = {L"设置", L"文件", L"编辑"};
        for (int i = 0; i < 3; ++i) app.ui.buttons[i].label = kDefLabels[i];
        LoadButtonLabels(app.ui.buttons.data(), static_cast<int>(app.ui.buttons.size()));
    }
    // 开源按钮字体：私有加载 资源/NotoSansSC-Regular.otf（OFL 协议；失败回退系统微软雅黑）
    {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
            wchar_t* slash = wcsrchr(exePath, L'\\');
            if (slash) {
                *(slash + 1) = L'\0';
                wcscat(exePath, L"资源\\NotoSansSC-Regular.otf");
                if (AddFontResourceExW(exePath, FR_PRIVATE, 0) > 0)
                    wcscpy(g_buttonFontName, L"Noto Sans SC");
            }
        }
    }
    // 按钮文字纹理：RasterizeText 光栅化 → 延迟上传（帧首 FlushPendingLabelUploads 真正上传）
    {
        g_stage = "按钮文字纹理";
        for (int i = 0; i < 3; ++i) {
            if (app.ui.buttons[i].label.empty()) continue;
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(app.ui.buttons[i].label.c_str(), 14, 2, g_buttonFontName, rgba, tw, th)) {
                UploadLabelRgba(app, app.ui.buttonLabels[i], rgba, tw, th);
            }
        }
        // 右部物体栏标题条文字「物体列表」（卡片列表控件方案B）
        {
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(L"物体列表", 12, 2, g_buttonFontName, rgba, tw, th)) {
                UploadLabelRgba(app, app.ui.objPanelTitle, rgba, tw, th);
            }
        }
    }

    {
        constexpr float kMenuPad = 10.0f, kMenuItemH = 30.0f, kItemGap = 5.0f;
        const float mx = static_cast<float>(app.ui.menuRect.offset.x);
        const float my = static_cast<float>(app.ui.menuRect.offset.y);
        const float mw = static_cast<float>(app.ui.menuRect.extent.width);
        const float itemY = my + kMenuPad;
        const float itemW = (mw - 2.0f * kMenuPad - kItemGap) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const float x = mx + kMenuPad + static_cast<float>(i) * (itemW + kItemGap);
            app.ui.menuItems[i].rect = {
                {static_cast<int32_t>(x), static_cast<int32_t>(itemY)},
                {static_cast<uint32_t>(itemW), static_cast<uint32_t>(kMenuItemH)}};
            app.ui.menuItems[i].radius = 2.0f;
            for (int j = 0; j < 4; ++j) {
                app.ui.menuItems[i].color[j] = app.ui.buttonTheme.normal[j];
                app.ui.menuItems[i].border[j] = kBorderColor.float32[j];
            }
        }
        app.ui.menuItems[0].onClick = [](App& a) {
            a.ui.menuOpen = false;
 // 点击导入 → 先弹出 500×700 默认色导入窗口（内容选择），
 // 窗口内"导入 3D 模型…"确认后经 g_importWindowConfirm 走原文件选择流程
            OpenImportWindow(a.hwnd);
        };
    }

    // 左上角软件图标（纯展示品牌图标，非按钮：无边框、不响应点击、无悬停高亮）
    {
        UiButton& a = app.ui.appIcon;
        a.rect = {{8, 4}, {28, 28}};  // 占位，ComputeTopBar 每帧覆盖
        a.radius = 4.0f;
        a.icon = 0;  // 齿轮
        a.onClick = nullptr;  // 纯展示品牌图标：不响应点击（打开设置由「设置」按钮负责）
        for (int i = 0; i < 4; ++i) {
            a.color[i] = app.ui.buttonTheme.normal[i];
            a.border[i] = kBorderColor.float32[i];
        }
    }
    // 右上角系统按钮：最小化/最大化/关闭（PS 风格，占位 rect 由 ComputeTopBar 覆盖）
    for (int i = 0; i < 3; ++i) {
        UiButton& s = app.ui.sysButtons[i];
        s.rect = {{0, 0}, {40, 36}};
        s.radius = 0.0f;
        s.icon = 0;
        for (int j = 0; j < 4; ++j) {
            s.color[j] = app.ui.buttonTheme.normal[j];
            s.border[j] = kBorderColor.float32[j];
        }
    }

    platform::ShowWindow(g_mainWindow, SW_SHOW);
    VkbLog("[loop] 进入主循环");

    MSG msg{};
    while (app.running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        { static int s_pp=0; if(s_pp<6){++s_pp; VkbLog("[post-pump]");} }
            if (msg.message == WM_QUIT) app.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (app.running) {
            // 物体栏单击超时补选（0.2s 内无第二击 → 第一击生效为选中；双击第二击走重命名）
            if (app.ui.objClickRow >= 0 &&
                app.ui.objClickRow < static_cast<int>(app.scene.objects.size()) &&
                GetTickCount64() - app.ui.objClickMs > 400) {
                const int row = app.ui.objClickRow;
                app.ui.objClickRow = -1;
                app.ui.multiSel.clear();
                if (app.scene.selectedObject == row) {
                    app.scene.selectedObject = -1;  // 点击已选中行 → 取消选择
                } else {
                    app.scene.selectedObject = row;
                }
                app.scene.wireframeSel = false;
            }
            // 窗口外 4px 热区：拖拽检测区 = 上边缘居中向外（窗口外上方 4px 也触发手势/拖拽）。
            // 鼠标在窗口外时消息到不了窗口，需主循环主动轮询光标：悬停显示手势；按住左键接管拖拽。
            // 注意：仅处理【窗口外】部分；窗口内 0~9px 由 WM_SETCURSOR/NCHITTEST 处理（避免抢按钮光标）。
            // 防误触：按下后位移 ≥3px 才真正启动拖拽，避免"点击窗口上方桌面"误拖动窗口。
            {
                static POINT sHotDown = {-100000, -100000};  // 热区按下起点（哨兵=未按下）
                POINT hp{}; GetCursorPos(&hp);
                RECT hwr{}; platform::GetWindowRect(g_mainWindow, &hwr);
                const bool inHot = (hp.y >= hwr.top - 4 && hp.y < hwr.top &&
                                    hp.x >= hwr.left && hp.x <= hwr.right);
                const bool lbtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (inHot) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // IDC_HAND 手势
                    if (lbtn && !app.ui.captionDragging && !app.ui.edgeResizing) {
                        if (sHotDown.x == -100000) {
                            sHotDown = hp;  // 记录按下起点
                        } else {
                            const int dx = hp.x - sHotDown.x, dy = hp.y - sHotDown.y;
                            if (dx * dx + dy * dy >= 9) {  // 位移 ≥3px 启动拖拽
                                sHotDown = {-100000, -100000};
                                app.ui.captionDragging = true;
                                app.ui.edgeResizing   = false;
                                app.ui.dragStartX = hp.x;
                                app.ui.dragStartY = hp.y;
                                platform::GetWindowRect(g_mainWindow, &app.ui.dragOrigRect);
                                // 最大化下延后还原：移动 ≥25px 才还原（<25px 视为单击不动）
                                if (app.ui.maximized) {
                                    app.ui.pendingRestore = true;
                                    platform::SetCapture(g_mainWindow);
                                    VkbLog("[dbg] HOTZONE drag start (maximized, pending)");
                                } else {
                                    platform::SetCapture(g_mainWindow);
                                    VkbLog("[dbg] HOTZONE drag start");
                                }
                            }
                        }
                    } else if (!lbtn) {
                        sHotDown = {-100000, -100000};  // 松开复位
                    }
                } else {
                    sHotDown = {-100000, -100000};  // 离开热区复位
                }
            }
            { static int s_run=0; if(s_run<6){++s_run; VkbLog("[run]");} }
            app.camera.UpdateSmooth();
            for (auto& b : app.ui.buttons) UpdateButtonColor(b, app.ui.buttonTheme);
            for (auto& b : app.ui.ballButtons) UpdateButtonColor(b, app.ui.buttonTheme);
            for (auto& b : app.ui.menuItems) UpdateButtonColor(b, app.ui.buttonTheme);
            UpdateButtonColor(app.ui.appIcon, app.ui.buttonTheme);
            for (int i = 0; i < 3; ++i) UpdateButtonColor(app.ui.sysButtons[i], app.ui.buttonTheme);
            {
                const float t = static_cast<float>(GetTickCount64() - app.ui.menuAnimStartMs) / 1000.0f;
                if (app.ui.menuOpen) {
                    const float k = std::min(t / 0.0375f, 1.0f);
                    app.ui.menuAnim = app.ui.menuAnimFrom + (1.0f - app.ui.menuAnimFrom) * k;
                } else {
                    if (t < 0.0125f) {
                        app.ui.menuAnim = app.ui.menuAnimFrom;
                    } else {
                        const float k = std::min((t - 0.0125f) / 0.025f, 1.0f);
                        app.ui.menuAnim = app.ui.menuAnimFrom * (1.0f - k);
                    }
                }
            }
 // 菜单完全关闭后：重置菜单项按钮状态（避免重新打开时残留 Hover/Pressed 高亮）
            if (!app.ui.menuOpen && app.ui.menuAnim < 0.01f) {
                for (int i = 0; i < 2; ++i) app.ui.menuItems[i].machine.state = ButtonState::Normal;
            }
 // 导入窗口确认"导入 3D 模型…"路径已选好，直接启动导入
            if (g_importWindowConfirm) {
                g_importWindowConfirm = false;
                if (!g_importPath.empty()) {
                    LaunchImport(app, g_importPath.c_str());
                    g_importPath.clear();
                }
            }
 // 导入窗口确认"地图导入(be)"：开窗(loading进度条，0%起)→后台线程验证/加载异步，不卡UI
            if (g_mcWorldConfirm) {
                g_mcWorldConfirm = false;
                if (!g_mcWorldPath.empty()) {
                    McWorldImporter::Instance().BeginLoad(app.hwnd, g_mcWorldPath);
                    g_mcWorldPath.clear();
                }
            }
 // 避免 GetOpenFileNameW 阻塞消息循环导致按钮动画卡在按下状态
            if (app.ui.pendingImport &&
                GetTickCount64() - app.ui.pendingImportAtMs >= 200) {
                app.ui.pendingImport = false;
                wchar_t path[MAX_PATH];
                if (PickModelFile(app.hwnd, path, MAX_PATH)) {
                    LaunchImport(app, path);
                }
            }
 // 物体显示栏标签右侧面板展示当前物体名称/位置
            UpdateObjectLabels(app);
            { static int s_ol=0; if(s_ol<6){++s_ol; VkbLog("[upd-objlbl]");} }
            { static int s_pred=0; if(s_pred<4){++s_pred; VkbLog("[pre-draw]");} }
            // 最小化期间停止渲染：最小化窗口交换链不可用，继续操作会触发驱动崩溃
            if (!app.ui.minimized) {
                DrawFrame(app);
            { static int s_postd=0; if(s_postd<4){++s_postd; VkbLog("[post-draw]");} }
            }
        }
    }

 // 主循环退出前先等后台导入线程，避免后台线程访问已销毁对象
    WaitForImportThread();
    vkDeviceWaitIdle(app.vk.device);
    Cleanup(app);
    platform::DestroyMainWindow(g_mainWindow);  // 释放主窗口（OS 边界收口）
    g_mainWindow = nullptr;
    return 0;
}

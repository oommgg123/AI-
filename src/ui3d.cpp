// ============================================================================
//  ui3d.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"
#include "ui_controls.h"  // 顺位布局 / 控件管理器（主视口面板 border 布局接入）
#include "import_pipeline.h"  // g_importProgress / g_importUploading（导入进度显示，原经 awa_internal.h 间接引入）
#include <initguid.h>
#include <wincodec.h>

using std::uint32_t;

std::string TrimStr(const std::string& s) {
    const char* ws = " \t\r";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}
ButtonTheme LoadButtonTheme() {
 // #214：默认主题单一来源 = ui::g_theme.button（ui_presets.h，COLORREF）；
 // 用户 button_theme.txt 若存在则逐键覆盖以下 float 值。
    ButtonTheme theme = FromUiButtonPreset(ui::g_theme.button);
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return theme;
    char exeUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exeUtf8, sizeof(exeUtf8), nullptr, nullptr);
    std::string path(exeUtf8);
    const size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos) path = path.substr(0, slash + 1);
    path += "button_theme.txt";

    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH);

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return theme;

    std::string content;
    char buf[512];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) content.append(buf, read);
    CloseHandle(h);

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, (nl == std::string::npos ? content.size() : nl) - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = TrimStr(line.substr(0, eq));
        const std::string val = TrimStr(line.substr(eq + 1));
        float v[3] = {0.0f, 0.0f, 0.0f};
        const int n = sscanf(val.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
        if (key == "normal" && n == 3) {
            theme.normal[0] = v[0]; theme.normal[1] = v[1]; theme.normal[2] = v[2];
        } else if (key == "pressed" && n == 3) {
            theme.pressed[0] = v[0]; theme.pressed[1] = v[1]; theme.pressed[2] = v[2];
        } else if (key == "released" && n == 3) {
            theme.released[0] = v[0]; theme.released[1] = v[1]; theme.released[2] = v[2];
        } else if (key == "hover_border" && n == 3) {
            theme.hoverBorder[0] = v[0]; theme.hoverBorder[1] = v[1]; theme.hoverBorder[2] = v[2];
        } else if (key == "anim_speed" && n >= 1) {
            theme.animSpeed = std::clamp(v[0], 0.0f, 1.0f);
        }
    }
    return theme;
}
// 读取 exe 同目录 button_labels.txt（UTF-8，key=label0/label1/label2）覆盖按钮文字。
// 未配置的键保留 buttons[i].label 默认值；文件缺失则全部保留默认。改文字后重启软件生效。
void LoadButtonLabels(UiButton* buttons, int n) {
    if (!buttons || n <= 0) return;
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    char exeUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exeUtf8, sizeof(exeUtf8), nullptr, nullptr);
    std::string path(exeUtf8);
    const size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos) path = path.substr(0, slash + 1);
    path += "button_labels.txt";

    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH);

    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    std::string content;
    char buf[512];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) content.append(buf, read);
    CloseHandle(h);

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, (nl == std::string::npos ? content.size() : nl) - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = TrimStr(line.substr(0, eq));
        const std::string val = TrimStr(line.substr(eq + 1));
        if (val.empty() || key.size() < 6 || key.compare(0, 5, "label") != 0) continue;
        const int idx = key[5] - '0';
        if (idx < 0 || idx >= n) continue;
        wchar_t wbuf[128];
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(), -1, wbuf, 128);
        buttons[idx].label = wbuf;
    }
}
void GetSettingsWPath(wchar_t out[MAX_PATH]) {
    out[0] = 0;
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    char exeUtf8[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exeUtf8, sizeof(exeUtf8), nullptr, nullptr);
    std::string path(exeUtf8);
    const size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos) path = path.substr(0, slash + 1);
    path += "awa_settings.txt";
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, out, MAX_PATH);
}
int LoadSettingInt(const char* key, int defaultValue) {
    wchar_t wpath[MAX_PATH];
    GetSettingsWPath(wpath);
    HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return defaultValue;

    std::string content;
    char buf[512];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) content.append(buf, read);
    CloseHandle(h);

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, (nl == std::string::npos ? content.size() : nl) - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (TrimStr(line.substr(0, eq)) == key) {
            int v = defaultValue;
            sscanf(line.substr(eq + 1).c_str(), "%d", &v);
            return v;
        }
    }
    return defaultValue;
}
void SaveSettingInt(const char* key, int value) {
    wchar_t wpath[MAX_PATH];
    GetSettingsWPath(wpath);
    std::string content;
    {
        HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[512];
            DWORD read = 0;
            while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0) content.append(buf, read);
            CloseHandle(h);
        }
    }
    char line[128];
    snprintf(line, sizeof(line), "%s=%d\r\n", key, value);
    std::string out;
    bool found = false;
    size_t pos = 0;
    while (pos < content.size()) {
        const size_t nl = content.find('\n', pos);
        std::string l = content.substr(pos, (nl == std::string::npos ? content.size() : nl) - pos);
        pos = (nl == std::string::npos) ? content.size() : nl + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq != std::string::npos && TrimStr(l.substr(0, eq)) == key) {
            out += line;
            found = true;
        } else if (!l.empty()) {
            out += l;
            out += "\r\n";
        }
    }
    if (!found) out += line;
    HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, out.c_str(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(h);
}
Layout ComputeLayout(const App& app) {
    const uint32_t w = app.vk.swapchainExtent.width;
    const uint32_t h = app.vk.swapchainExtent.height;
    const uint32_t topH    = (h > app.ui.panelTopH)    ? app.ui.panelTopH    : 0;
    const uint32_t bottomH = (h > app.ui.panelBottomH) ? app.ui.panelBottomH : 0;
    const uint32_t leftW   = (w > app.ui.panelLeftW)   ? app.ui.panelLeftW   : 0;
    const uint32_t rightW  = (w > app.ui.panelRightW)  ? app.ui.panelRightW  : 0;

    // 主视口面板接入顺位布局管理器（border 锚点：上/左/右/底/填充；与旧手动数学完全等价）
    ui::ControlManager mgr;
    mgr.add({"viewport", 0, ui::Axis::Vertical, ui::Anchor::Top,    0, static_cast<int>(topH),    -1, 0, 0});
    mgr.add({"viewport", 1, ui::Axis::Vertical, ui::Anchor::Left,   0, static_cast<int>(leftW),   -1, 0, 1});
    mgr.add({"viewport", 2, ui::Axis::Vertical, ui::Anchor::Right,  0, static_cast<int>(rightW),  -1, 0, 2});
    mgr.add({"viewport", 3, ui::Axis::Vertical, ui::Anchor::Bottom, 0, static_cast<int>(bottomH), -1, 0, 3});
    mgr.add({"viewport", 4, ui::Axis::Vertical, ui::Anchor::Fill,   0, -1,                        -1, 1, 4});
    const RECT client{0, 0, static_cast<int>(w), static_cast<int>(h)};
    mgr.compute(client);

    auto toVk = [](RECT r) -> VkRect2D {
        return {{r.left, r.top},
                {static_cast<uint32_t>(r.right - r.left), static_cast<uint32_t>(r.bottom - r.top)}};
    };
    auto R = [&](int id) -> RECT {
        const ui::Ctrl* c = mgr.find("viewport", id);
        return c ? c->rect : RECT{0, 0, 0, 0};
    };
    Layout l{};
    l.top      = toVk(R(0));
    l.left     = toVk(R(1));
    l.right    = toVk(R(2));
    l.bottom   = toVk(R(3));
    l.viewport = toVk(R(4));
    return l;
}

// 顶栏控件顺位布局（走控件渲染管线）：gear/edit/分隔线/move/rotate/scale 按顺位从左到右排列。
// 分隔线作为「控件」(1px) 参与布局，紧贴编辑按钮右侧（gap 小）；同时统一编辑按钮下拉菜单位置。
// 依赖：app.vk.swapchainExtent（窗口像素）、app.ui.panelTopH（顶栏高，>=36）。
void ComputeTopBar(App& app, const Layout& layout) {
    const uint32_t w = app.vk.swapchainExtent.width;
    const int topH = static_cast<int>(app.ui.panelTopH);
    const int btnTop = (topH - 30) / 2;
    const int divH   = 18, divTop = (topH - divH) / 2;

    // ---- 左侧：软件图标（无边框特殊按钮，awa logo，比工具按钮略大一点点）----
    const int appIconSize = 34;                       // 略大于工具按钮 30，放大一点点
    const int appIconTop  = (topH - appIconSize) / 2;
    app.ui.appIcon.rect = {{(int32_t)8, (int32_t)appIconTop},
                           {(uint32_t)appIconSize, (uint32_t)appIconSize}};

    // ---- 中部：设置/文件/编辑 宽按钮（buttons[0..2]，64×30：图标变小左置 + 文字右置）----
    // 软件图标与设置按钮间距 8px；宽按钮间距 8px（设置↔文件 间画分割线）
    const int wBtn = 64, hBtn = 30;
    const int gap = 8;
    int x = 8 + appIconSize + 8;  // 设置按钮起点：软件图标右缘 + 8px 间距
    std::vector<VkRect2D> iconRects;
    iconRects.push_back(app.ui.appIcon.rect);
    const int nTop = std::min(static_cast<int>(app.ui.buttons.size()), 3);
    for (int k = 0; k < nTop; ++k) {
        app.ui.buttons[k].rect = {{(int32_t)x, (int32_t)btnTop},
                                  {(uint32_t)wBtn, (uint32_t)hBtn}};
        iconRects.push_back(app.ui.buttons[k].rect);
        x += wBtn + gap;
    }
    // 相邻图标间隔中心画 1px 竖直分割线：仅 设置↔文件(i=1)；软件图标↔设置(i=0) 无分割线。
    // （编辑↔移动 分割线取消：移动按钮已移至左部面板垂直排列）
    app.ui.topDividers.clear();
    for (size_t i = 0; i + 1 < iconRects.size(); ++i) {
        if (i != 1) continue;
        const int midX = iconRects[i].offset.x +
                         static_cast<int32_t>(iconRects[i].extent.width) + gap / 2;
        app.ui.topDividers.push_back({{(int32_t)midX, (int32_t)divTop},
                                     {1u, (uint32_t)divH}});
    }

    // ---- 左部：移动/旋转/缩放（buttons[3..5]）垂直排列（30×30，间距 5px，顶部留白 10，水平居中）----
    if (app.ui.buttons.size() >= 6) {
        const VkRect2D& lp = layout.left;
        const int bw = 30;
        const int bx = lp.offset.x + (static_cast<int>(lp.extent.width) - bw) / 2;
        int by = lp.offset.y + 10;
        for (int k = 3; k < 6; ++k) {
            app.ui.buttons[k].rect = {{(int32_t)bx, (int32_t)by},
                                      {(uint32_t)bw, (uint32_t)bw}};
            by += bw + 5;
        }
    }

    // ---- 右侧：系统按钮 最小化/最大化/关闭（24×24，贴右上角：右缘 0 边距 + 上移 6→2，间距 4）----
    const int sysW = 24, sysH = 24;   // 小号 PS 风格按钮
    const int sysTop = 2;             // 贴顶（原垂直居中 6 → 2，更靠右上角）
    const int sysMargin = 0;          // 最右按钮贴窗口右缘（原 2 → 0）
    const int sysGap = 4;      // 相邻按钮间距（修复："过宽不居中"=按钮贴在一起像一整块）
    app.ui.sysButtons[2].rect = {{(int32_t)(w - sysMargin - sysW),                  sysTop}, {(uint32_t)sysW, (uint32_t)sysH}}; // 关闭(最右)
    app.ui.sysButtons[1].rect = {{(int32_t)(w - sysMargin - 2 * sysW - sysGap),     sysTop}, {(uint32_t)sysW, (uint32_t)sysH}}; // 最大化
    app.ui.sysButtons[0].rect = {{(int32_t)(w - sysMargin - 3 * sysW - 2 * sysGap), sysTop}, {(uint32_t)sysW, (uint32_t)sysH}}; // 最小化(最左)

    // 编辑按钮（buttons[2]）下拉菜单位置跟随（与 main.cpp init 逻辑一致）
    if (app.ui.buttons.size() >= 3) {
        const VkRect2D& eb = app.ui.buttons[2].rect;
        const int btnCenterX = eb.offset.x + static_cast<int32_t>(eb.extent.width) / 2;
        app.ui.menuRect = {{(int32_t)std::max(btnCenterX - 100, 0),
                            (int32_t)(eb.offset.y + static_cast<int32_t>(eb.extent.height) + 2)},
                           {200u, 0u}};
    }
    {
        constexpr float kMenuPad = 10.0f, kMenuItemH = 30.0f, kItemGap = 5.0f;
        const float mx = (float)app.ui.menuRect.offset.x;
        const float my = (float)app.ui.menuRect.offset.y;
        const float mw = (float)app.ui.menuRect.extent.width;
        const float itemY = my + kMenuPad;
        const float itemW = (mw - 2.0f * kMenuPad - kItemGap) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const float x2 = mx + kMenuPad + (float)i * (itemW + kItemGap);
            app.ui.menuItems[i].rect = {{(int32_t)x2, (int32_t)itemY},
                                        {(uint32_t)itemW, (uint32_t)kMenuItemH}};
        }
    }
}
int HitResizeDivider(const App& app, float mx, float my) {
    const Layout lay = ComputeLayout(app);
    constexpr float kTol = 6.0f;
    const float topY    = static_cast<float>(lay.top.extent.height);
    const float bottomY = static_cast<float>(lay.bottom.offset.y);
    const float leftX   = static_cast<float>(lay.left.extent.width);
    const float rightX  = static_cast<float>(lay.right.offset.x);
    if (my >= topY && my <= bottomY) {
        // 左栏固定 kLeftBarWidth 不可缩放（用户要求）——不再命中左栏右缘（div==1），无缩放光标/拖拽
 if (std::fabs(mx - rightX) <= kTol) return 2; // 右栏左缘（垂直）
    }
 if (std::fabs(my - bottomY) <= kTol) return 3; // 底栏上缘（全宽）
    return -1;
}
App* GetApp(HWND hwnd) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
LRESULT CALLBACK RenameEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = GetApp(GetParent(hwnd));
    if (msg == WM_KEYDOWN) {
 if (wParam == VK_RETURN) { if (app) ApplyRename(*app); return 0; } // 回车确认
 if (wParam == VK_ESCAPE) { if (app) CancelRename(*app); return 0; } // Esc 取消
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)),
                           hwnd, msg, wParam, lParam);
}
void OpenRenameEdit(App& app, int index) {
    if (app.ui.renameEdit) { DestroyWindow(app.ui.renameEdit); app.ui.renameEdit = nullptr; }
    app.ui.renameIndex = index;
    if (index < 0 || index >= static_cast<int>(app.scene.objects.size())) { app.ui.renameIndex = -1; return; }
    const SceneObject& o = app.scene.objects[static_cast<size_t>(index)];
    // 编辑框显示在右部物体栏目标行上（跟随卡片布局：标题条偏移 + 行间距）
    const Layout layR = ComputeLayout(app);
    const int ex = layR.right.offset.x + kObjPanelPad;
    const int ey = layR.right.offset.y + kObjPanelPad + kObjTitleH + index * (kObjPanelRowH + kObjRowGap);
    const int ew = static_cast<int>(layR.right.extent.width) - 2 * kObjPanelPad;
    const int eh = kObjPanelRowH;
    app.ui.renameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", o.name.c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     ex, ey, ew, eh, app.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!app.ui.renameEdit) { app.ui.renameIndex = -1; return; }
 // 子类化：GWLP_USERDATA 保存原 EDIT WndProc；回车确认 / Esc 取消
    SetWindowLongPtrW(app.ui.renameEdit, GWLP_USERDATA, GetWindowLongPtrW(app.ui.renameEdit, GWLP_WNDPROC));
    SetWindowLongPtrW(app.ui.renameEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditProc));
    SetFocus(app.ui.renameEdit);
 SendMessageW(app.ui.renameEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1)); // 全选旧名便于直接输入
}
void CancelRename(App& app) {
    if (app.ui.renameEdit) { DestroyWindow(app.ui.renameEdit); app.ui.renameEdit = nullptr; }
    app.ui.renameIndex = -1;
}
void ApplyRename(App& app) {
    if (!app.ui.renameEdit || app.ui.renameIndex < 0 ||
        app.ui.renameIndex >= static_cast<int>(app.scene.objects.size())) { CancelRename(app); return; }
    wchar_t buf[256];
    const int n = GetWindowTextW(app.ui.renameEdit, buf, 256);
    DestroyWindow(app.ui.renameEdit);
    app.ui.renameEdit = nullptr;
    const int idx = app.ui.renameIndex;
    app.ui.renameIndex = -1;
    if (n > 0) {
        SceneObject& o = app.scene.objects[static_cast<size_t>(idx)];
        const std::wstring newName(buf, static_cast<size_t>(n));
        if (o.name != newName) {
            o.name = newName;
 InvalidateRect(app.hwnd, nullptr, FALSE); // 触发重绘 → UpdateObjectLabels 检测名称变化重建标签
        }
    }
}
float MouseX(LPARAM lParam) { return static_cast<float>(static_cast<int16_t>(LOWORD(lParam))); }
float MouseY(LPARAM lParam) { return static_cast<float>(static_cast<int16_t>(HIWORD(lParam))); }
void DrawPanel(App& app, const PanelSpec& panel) {
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &panel.rect);

    PushConstants push{};
    push.rect[0] = static_cast<float>(panel.rect.offset.x);
    push.rect[1] = static_cast<float>(panel.rect.offset.y);
    push.rect[2] = static_cast<float>(panel.rect.extent.width);
    push.rect[3] = static_cast<float>(panel.rect.extent.height);
    push.fillColor[0] = panel.fill.float32[0];
    push.fillColor[1] = panel.fill.float32[1];
    push.fillColor[2] = panel.fill.float32[2];
    push.fillColor[3] = panel.fill.float32[3];
    push.borderColor[0] = panel.border.float32[0];
    push.borderColor[1] = panel.border.float32[1];
    push.borderColor[2] = panel.border.float32[2];
    push.borderColor[3] = panel.border.float32[3];
    push.cornerRadius = panel.radius;
    push.borderWidth = panel.borderWidth;
    push.mode = 0.0f;
    push.lineHalfWidth = 0.0f;

    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);
}
void DrawLine(App& app, VkRect2D scissor, float ax, float ay, float bx, float by,
              VkClearColorValue color, float halfWidth);

void DrawLetter(App& app, VkRect2D scissor, char c, float cx, float cy, float size,
                VkClearColorValue color) {
    const float s = size;
    const float half = 1.2f;
    switch (c) {
    case 'X':
        DrawLine(app, scissor, cx - s, cy - s, cx + s, cy + s, color, half);
        DrawLine(app, scissor, cx - s, cy + s, cx + s, cy - s, color, half);
        break;
    case 'Y':
        DrawLine(app, scissor, cx - s, cy - s, cx, cy, color, half);
        DrawLine(app, scissor, cx + s, cy - s, cx, cy, color, half);
        DrawLine(app, scissor, cx, cy, cx, cy + s, color, half);
        break;
    case 'Z':
        DrawLine(app, scissor, cx - s, cy - s, cx + s, cy - s, color, half);
        DrawLine(app, scissor, cx - s, cy + s, cx + s, cy + s, color, half);
        DrawLine(app, scissor, cx - s, cy + s, cx + s, cy - s, color, half);
        break;
    default:
        break;
    }
}
void DrawLine(App& app, VkRect2D scissor, float ax, float ay, float bx, float by,
              VkClearColorValue color, float halfWidth) {
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &scissor);

    PushConstants push{};
    push.rect[0] = ax;
    push.rect[1] = ay;
    push.rect2[0] = bx;
    push.rect2[1] = by;
    push.fillColor[0] = color.float32[0];
    push.fillColor[1] = color.float32[1];
    push.fillColor[2] = color.float32[2];
    push.fillColor[3] = color.float32[3];
    push.mode = 1.0f;
    push.lineHalfWidth = halfWidth;

    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);
}
bool RasterizeText(const wchar_t* text, int fontSize, int pad, const wchar_t* fontName,
                   std::vector<uint8_t>& rgba, int& width, int& height) {
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return false;

    HFONT measureFont = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontName);
    HGDIOBJ oldMF = SelectObject(hdc, measureFont);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &sz);
    SelectObject(hdc, oldMF);
    DeleteObject(measureFont);

    width  = sz.cx + pad * 2;
    height = sz.cy + pad * 2;
    if (width <= 0 || height <= 0) { DeleteDC(hdc); return false; }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(hdc); return false; }
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    std::memset(bits, 0, static_cast<size_t>(width) * height * 4);

    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontName);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    RECT rc{pad, pad, width - pad, height - pad};
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    rgba.resize(static_cast<size_t>(width) * height * 4);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (int i = 0; i < width * height; ++i) {
        const uint8_t b = p[i * 4 + 0], g = p[i * 4 + 1], r = p[i * 4 + 2];
        const uint8_t m1 = (r > g) ? r : g;
        const uint8_t a  = (m1 > b) ? m1 : b;
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }

    SelectObject(hdc, oldFont); DeleteObject(font);
    SelectObject(hdc, oldBmp); DeleteObject(bmp);
    DeleteDC(hdc);
    return true;
}
bool DecodePngWic(const unsigned char* pngData, size_t pngSize,
                  std::vector<uint8_t>& rgba, int& width, int& height,
                  int targetW, int targetH) {
    static const bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (!comReady) return false;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) { factory->Release(); return false; }
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(pngData),
                                      static_cast<DWORD>(pngSize));
    if (FAILED(hr)) { stream->Release(); factory->Release(); return false; }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr)) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr)) { factory->Release(); return false; }

    UINT w = 0, h = 0;
 // 可选 WIC 高质量缩放（球按钮图标 2160×2160 → 128×128，显存与纹理上传都省）
    IWICBitmapScaler* scaler = nullptr;
    if (targetW > 0 && targetH > 0) {
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(frame,
                                    static_cast<UINT>(targetW), static_cast<UINT>(targetH),
                                    WICBitmapInterpolationModeCubic);
        }
        if (FAILED(hr)) { scaler->Release(); frame->Release(); factory->Release(); return false; }
        w = static_cast<UINT>(targetW);
        h = static_cast<UINT>(targetH);
    } else {
        frame->GetSize(&w, &h);
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler ? static_cast<IWICBitmapSource*>(scaler)
                                          : static_cast<IWICBitmapSource*>(frame),
                                   GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }
    if (scaler) scaler->Release();
    frame->Release();

    if (FAILED(hr)) { factory->Release(); return false; }

    rgba.resize(static_cast<size_t>(w) * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data());
    converter->Release();
    factory->Release();
    if (FAILED(hr)) return false;

    for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    return true;
}
void DrawIcon(App& app, const VkRect2D& iconRect, VkClearColorValue color, VkDescriptorSet set, bool white) {
 if (app.vk.textPipeline == VK_NULL_HANDLE || set == VK_NULL_HANDLE) return; // 纹理降级保护
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &iconRect);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.textPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);
    vkCmdBindDescriptorSets(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            app.vk.textPipelineLayout, 0, 1, &set, 0, nullptr);
    TextPush tp{};
    tp.rect[0] = static_cast<float>(iconRect.offset.x);
    tp.rect[1] = static_cast<float>(iconRect.offset.y);
    tp.rect[2] = static_cast<float>(iconRect.extent.width);
    tp.rect[3] = static_cast<float>(iconRect.extent.height);
    tp.color[0] = color.float32[0];
    tp.color[1] = color.float32[1];
    tp.color[2] = color.float32[2];
    tp.color[3] = color.float32[3];
    // mono：白渲染通道=1（按钮图标默认），0=直通彩色（软件图标特殊通道，保留纹理真实颜色）
    tp.mono = white ? 1.0f : 0.0f;
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.textPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TextPush), &tp);
    vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);
}
void DrawMenu(App& app) {
    if (app.vk.menuPipeline == VK_NULL_HANDLE) return;
    constexpr float kMenuPad = 10.0f;
    constexpr float kMenuItemH = 30.0f;
    const float fullH = kMenuPad * 2.0f + kMenuItemH;
 // 底部白线 BUG 修复：menuAnim 接近 1 时 showH 截断（static_cast<uint32_t>）
    const float showH = (app.ui.menuAnim >= 0.999f) ? fullH : (fullH * app.ui.menuAnim);
    if (showH < 1.0f) return;

    const float mx = static_cast<float>(app.ui.menuRect.offset.x);
    const float my = static_cast<float>(app.ui.menuRect.offset.y);
    const float mw = static_cast<float>(app.ui.menuRect.extent.width);
    VkDeviceSize off = 0;
    const VkRect2D menuClip{
        {app.ui.menuRect.offset.x, app.ui.menuRect.offset.y},
        {app.ui.menuRect.extent.width, static_cast<uint32_t>(std::ceil(showH))}};

    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &menuClip);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.menuPipeline);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);

    PushConstants push{};
    push.rect[0] = mx;
    push.rect[1] = my;
    push.rect[2] = mw;
    push.rect[3] = fullH;
    push.fillColor[0] = kPanelColor.float32[0];
    push.fillColor[1] = kPanelColor.float32[1];
    push.fillColor[2] = kPanelColor.float32[2];
    push.fillColor[3] = kPanelColor.float32[3];
    push.borderColor[0] = kButtonIconColor.float32[0];
    push.borderColor[1] = kButtonIconColor.float32[1];
    push.borderColor[2] = kButtonIconColor.float32[2];
    push.borderColor[3] = kButtonIconColor.float32[3];
    push.cornerRadius = 3.0f;
    push.borderWidth = kLineWidth;
    push.mode = 0.0f;
    push.lineHalfWidth = 0.0f;
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.menuPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);

 // 套用三态动画（用 menuItems[i] 的过渡色/边框色绘制）；菜单展开到一定程度后才画（避免闪烁）
    if (app.ui.menuAnim > 0.2f) {
        const float iconSize = kMenuItemH - 2.0f;

        for (int i = 0; i < 2; ++i) {
            const UiButton& btn = app.ui.menuItems[i];
            const VkRect2D itemRect = btn.rect;

 // 重新绑定菜单管线 + 顶点缓冲（上一个按钮的 DrawIcon 已切到 textPipeline，必须重绑）
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.menuPipeline);
            vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);
            vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &itemRect);

            PushConstants itemPush{};
            itemPush.rect[0] = static_cast<float>(itemRect.offset.x);
            itemPush.rect[1] = static_cast<float>(itemRect.offset.y);
            itemPush.rect[2] = static_cast<float>(itemRect.extent.width);
            itemPush.rect[3] = static_cast<float>(itemRect.extent.height);
            itemPush.fillColor[0] = btn.color[0];
            itemPush.fillColor[1] = btn.color[1];
            itemPush.fillColor[2] = btn.color[2];
            itemPush.fillColor[3] = btn.color[3];
            itemPush.borderColor[0] = btn.border[0];
            itemPush.borderColor[1] = btn.border[1];
            itemPush.borderColor[2] = btn.border[2];
            itemPush.borderColor[3] = btn.border[3];
            itemPush.cornerRadius = btn.radius;
            itemPush.borderWidth = kLineWidth;
            itemPush.mode = 0.0f;
            itemPush.lineHalfWidth = 0.0f;
            vkCmdPushConstants(app.vk.commandBuffer, app.vk.menuPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(itemPush), &itemPush);
            vkCmdDraw(app.vk.commandBuffer, 6, 1, 0, 0);

            const VkDescriptorSet set = (i == 0) ? app.vk.importDescriptorSet : app.vk.exportDescriptorSet;
            if (set != VK_NULL_HANDLE) {
                const float iconX = static_cast<float>(itemRect.offset.x) +
                                    (static_cast<float>(itemRect.extent.width) - iconSize) * 0.5f;
                const float iconY = static_cast<float>(itemRect.offset.y) +
                                    (static_cast<float>(itemRect.extent.height) - iconSize) * 0.5f;
                const VkRect2D iconRect{
                    {static_cast<int32_t>(iconX), static_cast<int32_t>(iconY)},
                    {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
                DrawIcon(app, iconRect, kButtonIconColor, set);
            }
        }
    }
}
void FormatScaleLen(float len, char* buf, int n) {
    if (len >= 100.0f)      std::snprintf(buf, static_cast<size_t>(n), "%.0f", len);
    else if (len >= 10.0f)  std::snprintf(buf, static_cast<size_t>(n), "%.0f", len);
    else if (len >= 1.0f)   std::snprintf(buf, static_cast<size_t>(n), "%.1f", len);
    else                    std::snprintf(buf, static_cast<size_t>(n), "%.2f", len);
}
void UploadLabelRgba(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h);

// 比例尺**固定长条 + 移动竖线**——横条长度固定，竖线按相机距离
// （对数映射，范围 0.3~10000 与相机 zoom clamp 一致）左右移动指示当前缩放；
// 条上方居中显示当前距离数值（RasterizeText 系统字体，窄字 + 明显字间距）
static void DrawScaleBar(App& app, const Layout& layout) {
 // 淡入淡出动画（navZoomAlpha 驱动）——默认隐藏；滚轮缩放 0.1s 淡入、
 // 停止 0.5s 淡出、与罗盘/距离条 0.1s 互斥替换（UpdateNavHud 精确时间规则）
    if (app.ui.navZoomAlpha <= 0.004f) return;
    const float za = app.ui.navZoomAlpha;
    const VkRect2D& vp = layout.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return;

    const Vec3 r{app.camera.position.x - app.camera.target.x,
                 app.camera.position.y - app.camera.target.y,
                 app.camera.position.z - app.camera.target.z};
    const float dist = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (dist < 1e-4f) return;

 // 竖线位置：相机距离对数映射到固定条（近→左，远→右，平滑移动）
    constexpr float kMinDist = 0.3f;
 constexpr float kMaxDist = 1500.0f; // 与相机缩放最大值一致（10000→1500）
    const float lo = std::log10(kMinDist), hi = std::log10(kMaxDist);
    const float t = std::max(0.0f, std::min(1.0f, (std::log10(dist) - lo) / (hi - lo)));

 constexpr float kBarW = 140.0f; // 固定条长（不随缩放变化）
    const float margin = 16.0f;
    const float baseY = static_cast<float>(vp.offset.y + vp.extent.height) - margin;
    const float x0 = static_cast<float>(vp.offset.x) + margin;
    const float x1 = x0 + kBarW;
    const float tick = 5.0f;
 const VkClearColorValue trackCol{0.60f * za, 0.60f * za, 0.64f * za, za}; // 轨道（稍暗，颜色调制淡入淡出）
 const VkClearColorValue markCol{0.92f * za, 0.92f * za, 0.92f * za, za}; // 竖线/端刻线/数字

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.vk.swapchainExtent.width),
                        static_cast<float>(app.vk.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
    VkDeviceSize voff = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &voff);

 // 主横线（固定长，整体变细）
    DrawLine(app, vp, x0, baseY, x1, baseY, trackCol, 0.7f);
 // 两端竖刻线
    DrawLine(app, vp, x0, baseY - tick, x0, baseY + tick, markCol, 0.7f);
    DrawLine(app, vp, x1, baseY - tick, x1, baseY + tick, markCol, 0.7f);
 // 中间小刻度（25% / 50% / 75%）
    for (int k = 1; k <= 3; ++k) {
        const float sx = x0 + kBarW * (0.25f * k);
        DrawLine(app, vp, sx, baseY - 3.0f, sx, baseY + 3.0f, trackCol, 0.5f);
    }
 // 移动竖线（指示当前缩放，比轨道稍亮；变细）
    const float ix = x0 + t * kBarW;
    DrawLine(app, vp, ix, baseY - 8.0f, ix, baseY + 8.0f, markCol, 1.0f);

 // 数字标签（条上方居中）：系统默认字体（Segoe UI），替换七段数码
    if (app.vk.textPipeline != VK_NULL_HANDLE) {
        char buf[32];
        FormatScaleLen(dist, buf, sizeof(buf));
        wchar_t wbuf[32];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 32);
        if (app.ui.scaleLabel.text != wbuf) {
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(wbuf, 13, 2, L"Segoe UI", rgba, tw, th)) {
                UploadLabelRgba(app, app.ui.scaleLabel, rgba, tw, th);
                app.ui.scaleLabel.text = wbuf;
            }
        }
        if (app.ui.scaleLabel.set != VK_NULL_HANDLE) {
            const float wTxt = static_cast<float>(app.ui.scaleLabel.w);
            const float hTxt = static_cast<float>(app.ui.scaleLabel.h);
            const float lx = x0 + (kBarW - wTxt) * 0.5f;
            const float ly = baseY - tick - hTxt - 4.0f;
            const VkRect2D tRect{{static_cast<int32_t>(lx), static_cast<int32_t>(ly)},
                                 {static_cast<uint32_t>(wTxt), static_cast<uint32_t>(hTxt)}};
            VkClearColorValue col{{markCol.float32[0], markCol.float32[1], markCol.float32[2], za}};
            DrawIcon(app, tRect, col, app.ui.scaleLabel.set);
        }
    }
}
void UpdateBallButtons(App& app, const Layout& layout) {
    const VkRect2D& bvp = layout.viewport;
 // 按钮随窗口缩小而缩小（最多缩到默认的 1/2）——以首次视口尺寸为基准
    if (app.ui.ballRefW == 0 || app.ui.ballRefH == 0) {
        app.ui.ballRefW = bvp.extent.width;
        app.ui.ballRefH = bvp.extent.height;
    }
    float scale = 1.0f;
    if (app.ui.ballRefW > 0 && app.ui.ballRefH > 0 && bvp.extent.width > 0 && bvp.extent.height > 0) {
        scale = std::clamp(std::min(static_cast<float>(bvp.extent.width) / app.ui.ballRefW,
                                    static_cast<float>(bvp.extent.height) / app.ui.ballRefH),
 0.5f, 1.0f); // 最小 0.5（默认的一半），最大 1.0（不放大）
    }
 const float kBallR = 20.0f * scale; // 默认半径 20（15+5）；随窗口缩放
 const float kBallGap = 10.0f * scale; // 间距随缩放等比
    const float pitch = 2.0f * kBallR + kBallGap;
 const float bx0 = static_cast<float>(bvp.offset.x) + 8.0f * scale; // 更靠左上角
    const float by0 = static_cast<float>(bvp.offset.y) + 8.0f * scale;
    for (int i = 0; i < 3; ++i) {
        const float cx = bx0 + kBallR + pitch * i;
        const float cy = by0 + kBallR;
        app.ui.ballButtons[i].rect = {
            {static_cast<int32_t>(cx - kBallR), static_cast<int32_t>(cy - kBallR)},
            {static_cast<uint32_t>(2.0f * kBallR), static_cast<uint32_t>(2.0f * kBallR)}};
        app.ui.ballButtons[i].radius = kBallR;
 if (app.ui.ballButtons[i].color[3] == 0.0f) { // 首次：初始化为按钮主题 normal
            for (int c = 0; c < 4; ++c) {
                app.ui.ballButtons[i].color[c] = app.ui.buttonTheme.normal[c];
                app.ui.ballButtons[i].border[c] = app.ui.buttonTheme.hoverBorder[c];
            }
        }
    }
}
void UploadLabelRgbaNow(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h) {
 // 真正的 GPU 上传（销毁旧纹理+重建+拷贝）。仅从 FlushPendingLabelUploads 在帧首安全点调用，
 // 绝不在 DrawFrame 命令录制中途直接调用：旧代码在此 vkWaitForFences(inFlightFence,2s)，
 // 当时 inFlightFence 已复位、当前帧未提交→卡满 2 秒（#215 回归根因）。
    lt.w = lt.h = 0;
    if (w <= 0 || h <= 0 || rgba.empty()) return;
    if (lt.view)  vkDestroyImageView(app.vk.device, lt.view, nullptr);
    if (lt.image) vkDestroyImage(app.vk.device, lt.image, nullptr);
    if (lt.memory) vkFreeMemory(app.vk.device, lt.memory, nullptr);
    lt.view = VK_NULL_HANDLE; lt.image = VK_NULL_HANDLE; lt.memory = VK_NULL_HANDLE;
 // 描述集：首次分配一次，之后复用（绝不释放，避免 in-flight 引用 UB）
    if (lt.set == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo da{};
        da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        da.descriptorPool = app.vk.textDescriptorPool;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &app.vk.textDescriptorLayout;
        if (vkAllocateDescriptorSets(app.vk.device, &da, &lt.set) != VK_SUCCESS) return;
    }
 // ---- 上传纹理（staging → 设备本地图 → 视图）----
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    auto releaseStaging = [&]() {
        if (stagingBuffer) vkDestroyBuffer(app.vk.device, stagingBuffer, nullptr);
        if (stagingMemory) vkFreeMemory(app.vk.device, stagingMemory, nullptr);
    };
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(app.vk.device, &b, nullptr, &stagingBuffer) != VK_SUCCESS) return;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { releaseStaging(); return; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        if (vkAllocateMemory(app.vk.device, &a, nullptr, &stagingMemory) != VK_SUCCESS) { releaseStaging(); return; }
        vkBindBufferMemory(app.vk.device, stagingBuffer, stagingMemory, 0);
        void* data = nullptr;
        if (vkMapMemory(app.vk.device, stagingMemory, 0, imageSize, 0, &data) != VK_SUCCESS) { releaseStaging(); return; }
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.vk.device, stagingMemory);
    }
    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.format = VK_FORMAT_R8G8B8A8_UNORM;
    img.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(app.vk.device, &img, nullptr, &lt.image) != VK_SUCCESS) { releaseStaging(); return; }
    {
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(app.vk.device, lt.image, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (idx == UINT32_MAX) { vkDestroyImage(app.vk.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE; releaseStaging(); return; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        if (vkAllocateMemory(app.vk.device, &a, nullptr, &lt.memory) != VK_SUCCESS) { vkDestroyImage(app.vk.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE; releaseStaging(); return; }
        vkBindImageMemory(app.vk.device, lt.image, lt.memory, 0);
    }
    {
        VkCommandBuffer cmd = BeginOneTimeCommand(app);
        CmdImageBarrier(cmd, lt.image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, lt.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        CmdImageBarrier(cmd, lt.image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        EndOneTimeCommand(app, cmd);
    }
    releaseStaging();
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = lt.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(app.vk.device, &vi, nullptr, &lt.view) != VK_SUCCESS) {
        vkDestroyImage(app.vk.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE;
        vkFreeMemory(app.vk.device, lt.memory, nullptr); lt.memory = VK_NULL_HANDLE;
        return;
    }
 // 更新描述集指向新视图（复用 lt.set，不释放）
    VkDescriptorImageInfo dii{};
    dii.sampler = app.vk.textSampler;
    dii.imageView = lt.view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = lt.set;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.vk.device, 1, &wd, 0, nullptr);
    lt.w = w; lt.h = h;
}

void UploadLabelRgba(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h) {
    (void)app; // 暂存版不直接触碰 GPU，app 仅保持签名一致
 // 仅暂存待上传数据；真正的 GPU 纹理销毁/重建/拷贝延迟到 DrawFrame 帧首安全点
 // （FlushPendingLabelUploads）执行。原因：本函数常在 DrawFrame 命令录制中途被调用
 // （中键缩放改距离条文字 / 右键平移改坐标标签），若在此直接做 GPU 上传并等待栅栏会卡满 2 秒（#215 回归）。
    if (w <= 0 || h <= 0 || rgba.empty()) return;
    lt.pendingRgba = rgba;
    lt.pendingW = w;
    lt.pendingH = h;
    lt.needsUpload = true;
}

void FlushPendingLabelUploads(App& app) {
 // 帧首安全点：DrawFrame 已在第 508 行等待上一帧栅栏成功（上一帧已结束），
 // 当前帧尚未录制命令缓冲，此时销毁旧标签纹理、重建新纹理安全，不会破坏任何进行中的命令缓冲。
    App::LabelTexture* labels[] = {
        &app.ui.objNameLabel, &app.ui.objPanelTitle, &app.ui.objNameHighlight, &app.ui.scaleLabel, &app.ui.importUpLabel,
        &app.ui.coordLabels[0], &app.ui.coordLabels[1], &app.ui.coordLabels[2],
        &app.ui.buttonLabels[0], &app.ui.buttonLabels[1], &app.ui.buttonLabels[2],
    };
    for (App::LabelTexture* lt : labels) {
        if (!lt->needsUpload) continue;
        lt->needsUpload = false;
        UploadLabelRgbaNow(app, *lt, lt->pendingRgba, lt->pendingW, lt->pendingH);
    }
}
bool RasterizeNameList(const std::vector<std::wstring>& names, int width, int rowH,
                              std::vector<uint8_t>& rgba, int& outW, int& outH) {
    // 行间 kObjRowGap 间距（卡片列表）：总高 = n*rowH + (n-1)*gap
    const int n = static_cast<int>(names.size());
    const int h = n * rowH + (n > 1 ? (n - 1) * kObjRowGap : 0);
    if (h <= 0 || width <= 0) return false;
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return false;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(hdc); return false; }
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    std::memset(bits, 0, static_cast<size_t>(width) * h * 4);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    for (size_t i = 0; i < names.size(); ++i) {
        const int ry = static_cast<int>(i) * (rowH + kObjRowGap);
        RECT rc{2, ry, width - 2, ry + rowH};
        DrawTextW(hdc, names[i].c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    rgba.resize(static_cast<size_t>(width) * h * 4);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (int i = 0; i < width * h; ++i) {
        const uint8_t b = p[i * 4 + 0], g = p[i * 4 + 1], r = p[i * 4 + 2];
        const uint8_t m1 = (r > g) ? r : g;
        const uint8_t a = (m1 > b) ? m1 : b;
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = a;
    }
    SelectObject(hdc, oldBmp); DeleteObject(bmp);
    DeleteDC(hdc);
    outW = width; outH = h;
    return true;
}
void UpdateObjectLabels(App& app) {
    // 可见行 = 从 objScroll 起最多 kObjMaxRows 行（超出 5 栏滚动时仅重建可见行纹理）
    const int nAll = static_cast<int>(app.scene.objects.size());
    const int maxScroll = (nAll > kObjMaxRows) ? (nAll - kObjMaxRows) : 0;
    if (app.ui.objScroll > maxScroll) app.ui.objScroll = maxScroll;
    if (app.ui.objScroll < 0) app.ui.objScroll = 0;
    const int from = app.ui.objScroll;
    const int to = std::min(from + kObjMaxRows, nAll);

    std::vector<std::wstring> names;
    std::wstring key;
    for (int i = from; i < to; ++i) {
        names.push_back(app.scene.objects[static_cast<size_t>(i)].name);
        key += std::to_wstring(i) + L":" + app.scene.objects[static_cast<size_t>(i)].name + L"\n";
    }
    key += L"sel=" + std::to_wstring(app.scene.selectedObject);
    key += L"sc=" + std::to_wstring(app.ui.objScroll);
 if (key == app.ui.objNameLabel.text) return; // 未变化
    const uint64_t nowMs = GetTickCount64();
 if (nowMs - app.ui.objLabelThrottleMs < 100) return; // 节流
    app.ui.objLabelThrottleMs = nowMs;
 const Layout layLbl = ComputeLayout(app); // 跟随可调右栏布局
 const int kListW = static_cast<int>(layLbl.right.extent.width) - 2 * kObjPanelPad; // 面板内宽

 // 删除最后一个（或全部）模型后 names 为空，RasterizeNameList 因 h<=0 返回 false，
 // 若不显式清空，旧列表（最后一个模型名）会残留不消失。此处直接清空标签。
    if (names.empty()) {
        app.ui.objNameLabel.w = 0;
        app.ui.objNameLabel.h = 0;
 app.ui.objNameLabel.text = key; // 记录已处理，避免下一帧反复重试
        return;
    }

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!RasterizeNameList(names, kListW, kObjPanelRowH, rgba, w, h)) return;
    UploadLabelRgba(app, app.ui.objNameLabel, rgba, w, h);
    app.ui.objNameLabel.text = key;

    // 选中行加粗高亮文字（Blender Outliner 选中感：蓝条 + 加粗白字覆盖）
    {
        const int selGlobal = app.scene.selectedObject;
        const int selKey = (selGlobal >= from && selGlobal < to) ? (selGlobal - from) : -1;  // 可见区内索引
        std::wstring hkey = L"h:" + std::to_wstring(selGlobal) + L":" + std::to_wstring(app.ui.objScroll);
        if (selKey < 0 || selGlobal < 0 || selGlobal >= nAll) {
            // 选中行不可见：清空高亮纹理
            if (app.ui.objNameHighlight.text != hkey) {
                app.ui.objNameHighlight.w = 0; app.ui.objNameHighlight.h = 0; app.ui.objNameHighlight.text = hkey;
            }
        } else if (app.ui.objNameHighlight.text != hkey) {
            const std::wstring& selName = app.scene.objects[static_cast<size_t>(selGlobal)].name;
            // GDI 直接光栅化单行（FW_BOLD + 16px，与物体栏主题一致；纹理仅含选中行名字）
            HDC hdc = CreateCompatibleDC(nullptr);
            if (hdc) {
                HFONT hf = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
                if (hf) {
                    HGDIOBJ oldF = SelectObject(hdc, hf);
                    SIZE sz{}; GetTextExtentPoint32W(hdc, selName.c_str(), (int)selName.size(), &sz);
                    const int tw = sz.cx + 6, th = 22;
                    BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = tw; bmi.bmiHeader.biHeight = -th;
                    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
                    void* bits = nullptr;
                    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                    if (bmp) {
                        HGDIOBJ oldB = SelectObject(hdc, bmp);
                        std::memset(bits, 0, (size_t)tw * th * 4);
                        SetTextColor(hdc, RGB(255, 255, 255));
                        SetBkMode(hdc, TRANSPARENT);
                        RECT rc{3, 0, tw, th};
                        DrawTextW(hdc, selName.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                        std::vector<uint8_t> hrgba((size_t)tw * th * 4);
                        const uint8_t* p = (const uint8_t*)bits;
                        for (int i = 0; i < tw * th; ++i) {
                            const uint8_t b = p[i*4+0], g = p[i*4+1], r = p[i*4+2];
                            const uint8_t m1 = (r > g) ? r : g;
                            const uint8_t a = (m1 > b) ? m1 : b;
                            hrgba[i*4+0] = 255; hrgba[i*4+1] = 255; hrgba[i*4+2] = 255; hrgba[i*4+3] = a;
                        }
                        SelectObject(hdc, oldB); DeleteObject(bmp);
                        UploadLabelRgba(app, app.ui.objNameHighlight, hrgba, tw, th);
                        app.ui.objNameHighlight.text = hkey;
                        app.ui.objNameHighlight.w = tw; app.ui.objNameHighlight.h = th;
                    }
                    SelectObject(hdc, oldF); DeleteObject(hf);
                }
                DeleteDC(hdc);
            }
        }
    }
}
void UpdateNavHud(App& app) {
    static uint64_t s_lastFrameMs = 0;
    const uint64_t now = GetTickCount64();
    const float dt = s_lastFrameMs ? std::min(0.05f, static_cast<float>(now - s_lastFrameMs) / 1000.0f)
                                   : 0.016f;
    s_lastFrameMs = now;
    const float sinceMs = static_cast<float>(now - app.ui.navLastActionMs);
 const bool active = sinceMs < 500.0f; // 0.5s 无动作 → 开始淡出
    const int wantZoom = (active && app.ui.navLastActionType == 2) ? 1 : 0;
 const float inRate = 1.0f / 0.1f; // 出现 0.1s
 const float stopOut = 1.0f / 0.5f; // 无动作 0.5s 淡出
    if (wantZoom) {
        app.ui.navZoomAlpha = std::min(1.0f, app.ui.navZoomAlpha + dt * inRate);
    } else {
        app.ui.navZoomAlpha = std::max(0.0f, app.ui.navZoomAlpha - dt * stopOut);
    }
 // 摄像机坐标检测注视点 target 位移——旋转(orbit)/缩放(zoom) 都保持 target 固定，
 // 只有平移(pan) 才改它；故旋转视角时坐标不变，仅平移时淡入。
    const Vec3& cp = app.camera.target;
    const float dpos = std::sqrt((cp.x - app.ui.navCoordPrevPos.x) * (cp.x - app.ui.navCoordPrevPos.x)
                               + (cp.y - app.ui.navCoordPrevPos.y) * (cp.y - app.ui.navCoordPrevPos.y)
                               + (cp.z - app.ui.navCoordPrevPos.z) * (cp.z - app.ui.navCoordPrevPos.z));
    app.ui.navCoordPrevPos = cp;
    if (dpos > 0.001f) {
 // 平移中：满显，并刷新"最后移动时刻"
        app.ui.navCoordAlpha = 1.0f;
        app.ui.navCoordStopMs = now;
    } else {
 // 停止平移后先静待 0.5 秒（保持满显），之后 1.0s 淡出。
        const float sinceStop = static_cast<float>(now - app.ui.navCoordStopMs);
        if (sinceStop < 500.0f) {
 app.ui.navCoordAlpha = 1.0f; // 静待 0.5 秒
        } else {
 constexpr float coordFadeOut = 1.0f / 1.0f; // 淡出用 1.0s（静待 0.5s + 淡出 1s）
            app.ui.navCoordAlpha = std::max(0.0f, app.ui.navCoordAlpha - dt * coordFadeOut);
        }
    }
}
void DrawSelectionOutline(App& app, const float* mvp, const Layout& layout) {
    if (!mvp) return;
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    const SceneObject& so = app.scene.objects[static_cast<size_t>(app.scene.selectedObject)];
    if (so.boundsMin[0] > 1e29f) return;  // AABB 未计算
    const VkRect2D& vp = layout.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return;
    // 【关键修复】本函数在 DrawMenu 之后调用，而 DrawMenu 内部切换了 menu/text 管线未恢复——
    // DrawLine 不自行绑管线，必须在此显式恢复 2D 管线 + 顶点缓冲，否则选中框渲染错乱。
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
    VkDeviceSize voffSel = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &voffSel);
    float model[16], mvpm[16];
    BuildModelMatrix(so, model);
    MatMul4(mvp, model, mvpm);
    const float mn[3] = {so.boundsMin[0], so.boundsMin[1], so.boundsMin[2]};
    const float mx[3] = {so.boundsMax[0], so.boundsMax[1], so.boundsMax[2]};
    float c[8][4];
    int idx = 0;
    for (int zi = 0; zi < 2; ++zi) for (int yi = 0; yi < 2; ++yi) for (int xi = 0; xi < 2; ++xi) {
        const float lx = xi ? mx[0] : mn[0], ly = yi ? mx[1] : mn[1], lz = zi ? mx[2] : mn[2];
        const float v[4] = {lx, ly, lz, 1.0f};
        float r[4] = {0,0,0,0};
        for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) r[j] += mvpm[j*4+k] * v[k];
        c[idx][0] = r[0]; c[idx][1] = r[1]; c[idx][2] = r[2]; c[idx][3] = r[3];
        ++idx;
    }
    bool valid[8];
    auto proj = [&](int i, float& x, float& y) {
        if (c[i][3] <= 0) { valid[i] = false; x = -1e9f; y = -1e9f; return; }  // 近平面后角：标记无效
        valid[i] = true;
        const float w = c[i][3];
        x = (c[i][0] / w * 0.5f + 0.5f) * (float)vp.extent.width + (float)vp.offset.x;
        y = (1.0f - (c[i][1] / w * 0.5f + 0.5f)) * (float)vp.extent.height + (float)vp.offset.y;
    };
    float p[8][2];
    for (int i = 0; i < 8; ++i) proj(i, p[i][0], p[i][1]);
    // clamp 屏幕坐标到 viewport 范围 + 余量（避免大模型顶点延伸无穷远；类似 Blender 选中框延伸被裁剪）
    const float vw = (float)vp.extent.width, vh = (float)vp.extent.height;
    const float margin = std::max(vw, vh) * 0.25f;
    const float xmin = (float)vp.offset.x - margin, xmax = (float)vp.offset.x + vw + margin;
    const float ymin = (float)vp.offset.y - margin, ymax = (float)vp.offset.y + vh + margin;
    for (int i = 0; i < 8; ++i) {
        p[i][0] = std::clamp(p[i][0], xmin, xmax);
        p[i][1] = std::clamp(p[i][1], ymin, ymax);
    }
    static const int edges[12][2] = {
        {0,1},{1,3},{3,2},{2,0},
        {4,5},{5,7},{7,6},{6,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    const VkClearColorValue yb{{1.0f, 0.84f, 0.1f, 1.0f}};
    for (auto& e : edges) {
        if (!valid[e[0]] || !valid[e[1]]) continue;  // 两端都须在视锥内（近平面后角不连乱线）
        DrawLine(app, vp, p[e[0]][0], p[e[0]][1], p[e[1]][0], p[e[1]][1], yb, 1.2f);
    }
}
void DrawLogicBar(App& app, const Layout& layout, const float* mvp) {
    const uint32_t w = app.vk.swapchainExtent.width;
    const uint32_t h = app.vk.swapchainExtent.height;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);

    std::array<PanelSpec, 12> panels{};
    uint32_t panelCount = 0;
    const auto addPanel = [&](VkRect2D r, VkClearColorValue fill, float radius) {
        if (r.extent.width > 0 && r.extent.height > 0) panels[panelCount++] = {r, fill, radius};
    };
    addPanel(layout.top, kPanelColor, 0.0f);
    addPanel(layout.left, kPanelColor, kCornerRadius);
 addPanel(layout.right, kPanelColor, 0.0f); // 右部模型栏无圆角
 addPanel(layout.bottom, kPanelColor, kCornerRadius); // 底部面板（默认高 150，可拖分隔线调整）
 // 4 条可拖拽分隔线（1px 边框色，可视化可缩放边缘）
    addPanel({{0, static_cast<int32_t>(layout.top.extent.height) - 1}, {w, 1}},
             kBorderColor, 0.0f);
    addPanel({{0, static_cast<int32_t>(layout.bottom.offset.y)}, {w, 1}},
             kBorderColor, 0.0f);
    addPanel({{static_cast<int32_t>(layout.left.extent.width) - 1, static_cast<int32_t>(layout.left.offset.y)},
              {1, layout.left.extent.height}}, kBorderColor, 0.0f);
    addPanel({{static_cast<int32_t>(layout.right.offset.x), static_cast<int32_t>(layout.right.offset.y)},
              {1, layout.right.extent.height}}, kBorderColor, 0.0f);
    addPanel({{0, static_cast<int32_t>(h - kLineWidth)}, {w, 1}},
             kBorderColor, 0.0f);
    for (uint32_t i = 0; i < panelCount; ++i) DrawPanel(app, panels[i]);

 // 分隔线高亮：仅按下拖拽（app.ui.resizeDrag>=1）才画黄色亮线，悬停不再高亮；
 // 顶栏(div 0)不可拖不高亮；左栏右缘/右栏左缘/底栏上缘对应 div 1/2/3
    {
        const int div = app.ui.resizeDrag; // 由悬停检测(HitResizeDivider)改为按下拖拽态
        if (div >= 1) {
            const VkClearColorValue kYellow = {{1.0f, 0.84f, 0.1f, 1.0f}};
            const VkRect2D scissorAll{{0, 0}, {w, h}};
            const float topY = static_cast<float>(layout.top.extent.height);
            const float bottomY = static_cast<float>(layout.bottom.offset.y);
            const float leftX = static_cast<float>(layout.left.extent.width);
            const float rightX = static_cast<float>(layout.right.offset.x);
            if (div == 1)
 DrawLine(app, scissorAll, leftX - 1.0f, topY, leftX - 1.0f, bottomY, kYellow, 1.0f); // 左栏右缘
            else if (div == 2)
 DrawLine(app, scissorAll, rightX, topY, rightX, bottomY, kYellow, 1.0f); // 右栏左缘
            else
 DrawLine(app, scissorAll, 0.0f, bottomY, static_cast<float>(w), bottomY, kYellow, 1.0f); // 底栏上缘
        }
    }

    // 顶栏控件顺位布局（走控件渲染管线）：gear/edit/分隔线/move/rotate/scale 按顺位排列。
    // 统一按钮 rect、分隔线控件 rect、编辑按钮下拉菜单位置（单一来源）。
    ComputeTopBar(app, layout);

    for (size_t bi = 0; bi < app.ui.buttons.size(); ++bi) {
        const UiButton& btn = app.ui.buttons[bi];
        VkClearColorValue fill{};
        VkClearColorValue border{};
        for (int i = 0; i < 4; ++i) {
            fill.float32[i] = btn.color[i];
            border.float32[i] = btn.border[i];
        }
 // 变换模式按钮（后 3 个=移动/旋转/缩放，现位于左部）——当前 gizmoMode 激活提亮（视觉反馈）
        if (app.ui.buttons.size() >= 3 && bi >= app.ui.buttons.size() - 3 &&
            static_cast<int>(bi - (app.ui.buttons.size() - 3)) == app.ui.gizmoMode) {
            for (int c = 0; c < 3; ++c)
                fill.float32[c] = std::min(1.0f, fill.float32[c] * 1.35f + 0.1f);
        }
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        DrawPanel(app, {btn.rect, fill, btn.radius, border});
        if (app.vk.textPipeline == VK_NULL_HANDLE) continue;
        const float cy = static_cast<float>(btn.rect.offset.y) + btn.rect.extent.height * 0.5f;
        if (bi < 3) {
            // 顶栏宽按钮（设置/文件/编辑）：16px 小图标左置 + 按钮文字右置（button_labels.txt 可改）
            const float iconSize = 16.0f;
            const float ix = static_cast<float>(btn.rect.offset.x) + 10.0f;
            const VkRect2D iconRect{
                {static_cast<int32_t>(ix - iconSize * 0.5f), static_cast<int32_t>(cy - iconSize * 0.5f)},
                {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
            VkDescriptorSet set;
            if (btn.icon == 1)      set = app.vk.penDescriptorSet;
            else if (btn.icon == 2) set = app.vk.fileDescriptorSet;
            else                    set = app.vk.textDescriptorSet;
            DrawIcon(app, iconRect, kButtonIconColor, set);
            const App::LabelTexture& lt = app.ui.buttonLabels[bi];
            if (lt.set != VK_NULL_HANDLE && lt.w > 0 && lt.h > 0) {
                const VkRect2D tRect{
                    {static_cast<int32_t>(btn.rect.offset.x) + 30,
                     static_cast<int32_t>(cy - lt.h * 0.5f)},
                    {static_cast<uint32_t>(lt.w), static_cast<uint32_t>(lt.h)}};
                DrawIcon(app, tRect, kButtonIconColor, lt.set);
            }
        } else {
            // 左部变换按钮（移动/旋转/缩放）：图标居中
            const float iconSize = std::min(static_cast<float>(btn.rect.extent.height) - 6.0f, 48.0f);
            const float cx = static_cast<float>(btn.rect.offset.x) + btn.rect.extent.width * 0.5f;
            const VkRect2D iconRect{
                {static_cast<int32_t>(cx - iconSize * 0.5f), static_cast<int32_t>(cy - iconSize * 0.5f)},
                {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
            const int ti = static_cast<int>(bi - (app.ui.buttons.size() - 3));
            VkDescriptorSet set = app.ui.transformIcons[ti].valid ? app.ui.transformIcons[ti].set : app.vk.textDescriptorSet;
            DrawIcon(app, iconRect, kButtonIconColor, set);
        }
    }

    {
        // 顶栏图标间分割线（1px 竖直控件，由 ComputeTopBar 计算）
        const VkClearColorValue kTopDivColor{{0.5f, 0.5f, 0.5f, 1.0f}};
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        for (const VkRect2D& d : app.ui.topDividers) {
            if (d.extent.width > 0 && d.extent.height > 0) DrawPanel(app, {d, kTopDivColor, 0.0f});
        }
    }

    // 左上角软件图标（纯展示品牌图标，非按钮：仅绘制图标，无悬停/按下高亮背景）
    {
        const UiButton& a = app.ui.appIcon;
        if (app.vk.textPipeline != VK_NULL_HANDLE) {
            const float iconSize = std::min(static_cast<float>(a.rect.extent.height) - 8.0f, 48.0f);
            const float cx = static_cast<float>(a.rect.offset.x) + a.rect.extent.width * 0.5f;
            const float cy = static_cast<float>(a.rect.offset.y) + a.rect.extent.height * 0.5f;
            const VkRect2D iconRect{{static_cast<int32_t>(cx - iconSize * 0.5f),
                                     static_cast<int32_t>(cy - iconSize * 0.5f)},
                                    {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
            DrawIcon(app, iconRect, kButtonIconColor, app.vk.appIconDescriptorSet, false); // false=特殊通道，保留 awa logo 真实颜色
        }
    }

 // 3D 视口左上角 3 个圆形按钮与默认按钮一致——先画按钮(底+描边) 再画图标，图标不被描边遮挡
    UpdateBallButtons(app, layout);
    {
        const VkRect2D& bvp = layout.viewport;
        if (bvp.extent.width > 0 && bvp.extent.height > 0) {
            for (int i = 0; i < 3; ++i) {
                const UiButton& b = app.ui.ballButtons[i];
 // 当前激活的渲染模式球 → 提亮 + 1.5px 黄描边
                const bool active = (i == 0 && app.ui.renderMode == 1) || (i == 1 && app.ui.renderMode == 0);
 // hover 状态（标准按钮动画）
                const bool hover = (b.machine.state == ButtonState::Hover);
 // 1) 按钮圆底 + 描边（先画；fill 用按钮色，描边黄/主题色；撤销彩虹环恢复统一样式）
                vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);
                VkClearColorValue fill{};
                for (int c = 0; c < 4; ++c) fill.float32[c] = b.color[c];
                if (active) {
                    for (int c = 0; c < 3; ++c)
                        fill.float32[c] = std::min(1.0f, fill.float32[c] * 1.4f + 0.15f);
 const VkClearColorValue yb{{1.0f, 0.84f, 0.1f, 1.0f}}; // 黄边（选中态）
                    DrawPanel(app, {b.rect, fill, b.radius, yb, 1.5f});
                } else if (hover) {
                    const VkClearColorValue hb{{b.border[0], b.border[1], b.border[2], 1}};
                    DrawPanel(app, {b.rect, fill, b.radius, hb, 1.0f});
                } else {
 DrawPanel(app, {b.rect, fill, b.radius, fill, 0.0f}); // 纯底无描边
                }
 // 2) 图标叠加（最后画 → 永远在最上层，不被黄边/描边遮挡）
                if (app.ui.ballIcons[i].valid) {
 const uint32_t isz = static_cast<uint32_t>(b.rect.extent.width) * 4u / 5u; // 30→24px
                    const int32_t ix = b.rect.offset.x +
                                       (static_cast<int32_t>(b.rect.extent.width) - static_cast<int32_t>(isz)) / 2;
                    const int32_t iy = b.rect.offset.y +
                                       (static_cast<int32_t>(b.rect.extent.height) - static_cast<int32_t>(isz)) / 2;
                    const VkRect2D iconRect{{ix, iy}, {isz, isz}};
                    DrawIcon(app, iconRect, VkClearColorValue{{1, 1, 1, 1}}, app.ui.ballIcons[i].set);
                }
            }
        }
    }

 if ((g_importProgress >= 0 && g_importProgress < 100) || g_importUploading.load() == 1) { // GPU 上传阶段进度条保持显示
        const VkRect2D& vp = layout.viewport;
        if (vp.extent.width > 0 && vp.extent.height > 0) {
            const uint64_t nowMs = GetTickCount64();
            const float bw = std::min(560.0f, static_cast<float>(vp.extent.width) * 0.7f);  // 520→560 加大
            const float bh = 18.0f;                                                          // 12→18 加粗
            // 重做：移到【视口顶部居中下方 40px】（更醒目；不再贴视口底部易被忽略）
            const float bx = static_cast<float>(vp.offset.x) + (static_cast<float>(vp.extent.width) - bw) * 0.5f;
            const float by = static_cast<float>(vp.offset.y) + 40.0f;
            const float realProg = static_cast<float>(g_importProgress.load());
            app.ui.importDisplayProg = std::max(app.ui.importDisplayProg, realProg);
            app.ui.importDisplayProg += (realProg - app.ui.importDisplayProg) * 0.2f;
            const float prog = std::clamp(app.ui.importDisplayProg, 0.0f, 100.0f);
 VkClearColorValue bgCol = ThemeColor(ui::g_theme.slider.track, 0.85f); // 进度条轨道底色
 VkClearColorValue fillCol = ThemeColor(ui::g_theme.slider.fill); // 进度条填充（accent）
            // 重做：进度条背景半透明黑底（与 3D 视口区分，提高可读性）
            const VkClearColorValue panelBack{{0.0f, 0.0f, 0.0f, 0.55f}};
            const float padX = 24.0f, padY = 18.0f;
            const VkRect2D backRect{{static_cast<int32_t>(bx - padX), static_cast<int32_t>(by - padY)},
                                    {static_cast<uint32_t>(bw + 2.0f * padX),
                                     static_cast<uint32_t>(bh + 2.0f * padY + 28.0f)}};  // 高度+文字区
            DrawPanel(app, {backRect, panelBack, 6.0f});
            const VkRect2D barRect{{static_cast<int32_t>(bx), static_cast<int32_t>(by)},
                                   {static_cast<uint32_t>(bw), static_cast<uint32_t>(bh)}};
            DrawPanel(app, {barRect, bgCol, 9.0f});  // 6→9
            if (prog > 1.0f) {
                const float fillW = bw * prog / 100.0f;
                const VkRect2D fillRect{{static_cast<int32_t>(bx), static_cast<int32_t>(by)},
                                        {static_cast<uint32_t>(fillW), static_cast<uint32_t>(bh)}};
                DrawPanel(app, {fillRect, fillCol, 9.0f});
            }
            {
                const float cx = bx - 36.0f;          // 留出更大圆位
                const float cy = by + bh * 0.5f;
                const float rad = 22.0f;             // 16→22
                const int segs = 12;
                const float kTwoPi = 6.2831853f;
                const float spin = static_cast<float>(nowMs % 500) / 500.0f * kTwoPi;
                for (int i = 0; i < segs; ++i) {
                    const float ang = static_cast<float>(i) / static_cast<float>(segs) * kTwoPi - spin;
                    const float a0 = ang - kTwoPi / static_cast<float>(segs) * 0.45f;
                    const float a1 = ang + kTwoPi / static_cast<float>(segs) * 0.45f;
                    const float headAngle = std::fmod(-spin + kTwoPi, kTwoPi);
                    float lag = std::fmod(static_cast<float>(i) / static_cast<float>(segs) * kTwoPi - headAngle + kTwoPi, kTwoPi);
                    if (lag > kTwoPi * 0.5f) lag = kTwoPi - lag;
                    float bright = 1.0f - std::min(lag, kTwoPi * 0.5f) / (kTwoPi * 0.5f);
                    bright = std::clamp(bright * 1.6f, 0.0f, 1.0f);
                    VkClearColorValue segCol{};
                    for (int c = 0; c < 3; ++c) segCol.float32[c] = fillCol.float32[c] * (0.25f + 0.75f * bright);
                    segCol.float32[3] = 0.25f + 0.75f * bright;
                    DrawLine(app, vp, cx + std::cos(a0) * rad, cy + std::sin(a0) * rad,
                                    cx + std::cos(a1) * rad, cy + std::sin(a1) * rad,
                                    segCol, 3.0f);  // 2.6→3.0 加粗
                }
            }
 // GPU 上传阶段提示文字（重做：15→16 加大；位置对齐新顶部）
            if (g_importUploading.load() == 1) {
                const wchar_t* msg = L"正在上传渲染数据…";
                if (app.ui.importUpLabel.text != msg) {
                    std::vector<uint8_t> rgba;
                    int tw = 0, th = 0;
                    if (RasterizeText(msg, 16, 2, L"Segoe UI", rgba, tw, th))
                        UploadLabelRgba(app, app.ui.importUpLabel, rgba, tw, th);
                }
                if (app.ui.importUpLabel.set != VK_NULL_HANDLE && app.ui.importUpLabel.w > 0) {
                    const VkRect2D tRect{{static_cast<int32_t>(bx + (bw - static_cast<float>(app.ui.importUpLabel.w)) * 0.5f),
                                          static_cast<int32_t>(by - 24.0f)},
                                         {static_cast<uint32_t>(app.ui.importUpLabel.w),
                                          static_cast<uint32_t>(app.ui.importUpLabel.h)}};
                    DrawIcon(app, tRect, VkClearColorValue{{0.9f, 0.9f, 0.9f, 1.0f}}, app.ui.importUpLabel.set);
                }
            }
        }
    }

 // 3D 视口比例尺左下角，表示当前摄像机缩放大小
    DrawScaleBar(app, layout);

 // 左键框选矩形（半透明蓝填充 + 白色细边）
    if (app.ui.marqueeSelecting) {
        const VkRect2D& vp = layout.viewport;
        const float m0x = std::min(app.ui.marqueeX0, app.ui.marqueeX1);
        const float m0y = std::min(app.ui.marqueeY0, app.ui.marqueeY1);
        const float m1x = std::max(app.ui.marqueeX0, app.ui.marqueeX1);
        const float m1y = std::max(app.ui.marqueeY0, app.ui.marqueeY1);
        if (m1x - m0x >= 1.0f && m1y - m0y >= 1.0f) {
 const VkClearColorValue fill{{0.15f, 0.45f, 0.90f, 0.35f}}; // 半透明蓝（alpha=0.35，混合管线生效）
            const VkClearColorValue noBorder{{0, 0, 0, 0}};
            PanelSpec marq{};
            marq.rect = {{static_cast<int32_t>(m0x), static_cast<int32_t>(m0y)},
                         {static_cast<uint32_t>(m1x - m0x), static_cast<uint32_t>(m1y - m0y)}};
            marq.fill = fill;
            marq.radius = 0.0f;
            marq.border = noBorder;
            marq.borderWidth = 0.0f;
            if (app.vk.pipelinePanelBlend != VK_NULL_HANDLE) {
                vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelinePanelBlend);
            }
            DrawPanel(app, marq);
            const VkClearColorValue bcol{{1.0f, 1.0f, 1.0f, 1.0f}};
            DrawLine(app, vp, m0x, m0y, m1x, m0y, bcol, 0.8f);
            DrawLine(app, vp, m1x, m0y, m1x, m1y, bcol, 0.8f);
            DrawLine(app, vp, m1x, m1y, m0x, m1y, bcol, 0.8f);
            DrawLine(app, vp, m0x, m1y, m0x, m0y, bcol, 0.8f);
 // 恢复默认 2D 管线（半透明混合管线仅框选填充用）
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
        }
    }


 // 右侧物体显示栏多物体名称列表，Blender 风格；点击行选中
 // 跟随可调右栏布局（layout 为 ComputeLayout 单一来源）
 // 方案B 卡片列表控件：面板圆角 8px + 顶部标题条「物体列表」+ 行间 2px 间距
    if (layout.right.extent.width >= 40 && layout.right.extent.height >= 100) {
        const int px = layout.right.offset.x + kObjPanelPad;
        const int py = layout.right.offset.y + kObjPanelPad;
        const int pw = static_cast<int>(layout.right.extent.width) - 2 * kObjPanelPad;
        // 面板高度固定：标题条 + 约 5 栏行高 + 边距（用户要求固定高度约 5 栏，不随物体数变化）
        const int ph = kObjTitleH + kObjMaxRows * (kObjPanelRowH + kObjRowGap) + 2 * kObjPanelPad;
        const VkClearColorValue panelFill = {{0.0f, 0.0f, 0.0f, 1.0f}};  // 与 3D 视口一样黑
        const VkClearColorValue whiteBorder = {{1.0f, 1.0f, 1.0f, 1.0f}};
        const VkClearColorValue selColor = ThemeColor(ui::g_theme.list.selected);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);
        // 面板：整体圆角 4px（卡片风格，降低弧度）
        DrawPanel(app, {{{px - kObjPanelPad, py - kObjPanelPad},
                         {static_cast<uint32_t>(pw + 2 * kObjPanelPad), static_cast<uint32_t>(ph)}},
                 panelFill, 4.0f, whiteBorder, 1.0f});
        // 顶部标题条「物体列表」：比黑面板略亮的深色 + 白字（圆角顶）
        {
            const VkRect2D titleRect{{px - kObjPanelPad, py - kObjPanelPad},
                                     {static_cast<uint32_t>(pw + 2 * kObjPanelPad), static_cast<uint32_t>(kObjTitleH)}};
            const VkClearColorValue tf{{0.11f, 0.12f, 0.14f, 1.0f}};
            DrawPanel(app, {titleRect, tf, 4.0f, VkClearColorValue{{0, 0, 0, 0}}, 0.0f});
            if (app.ui.objPanelTitle.set != VK_NULL_HANDLE && app.ui.objPanelTitle.w > 0) {
                const VkRect2D tRect{
                    {titleRect.offset.x + (static_cast<int32_t>(titleRect.extent.width) - app.ui.objPanelTitle.w) / 2,
                     titleRect.offset.y + (kObjTitleH - app.ui.objPanelTitle.h) / 2},
                    {static_cast<uint32_t>(app.ui.objPanelTitle.w), static_cast<uint32_t>(app.ui.objPanelTitle.h)}};
                DrawIcon(app, tRect, kButtonIconColor, app.ui.objPanelTitle.set);
            }
        }
 // 物体栏行（Blender Outliner 风格）：仅画可见 kObjMaxRows 行（从 objScroll 起）；选中=主题选中色整行，悬停=淡亮
        const int listY = py - kObjPanelPad + kObjTitleH;
        const int nRows = static_cast<int>(app.scene.objects.size());
        const int listH = kObjMaxRows * (kObjPanelRowH + kObjRowGap);
        const int maxScroll = (nRows > kObjMaxRows) ? (nRows - kObjMaxRows) : 0;
        if (app.ui.objScroll > maxScroll) app.ui.objScroll = maxScroll;
        if (app.ui.objScroll < 0) app.ui.objScroll = 0;
        const VkClearColorValue kNoBorder{{0, 0, 0, 0}};
        for (int vi = 0; vi < kObjMaxRows; ++vi) {
            const int i = app.ui.objScroll + vi;   // 全局物体索引
            if (i >= nRows) break;
            const int ry = listY + vi * (kObjPanelRowH + kObjRowGap);
            const VkRect2D rowRect{{px, ry},
                                   {static_cast<uint32_t>(pw - (maxScroll > 0 ? 9 : 0)),
                                    static_cast<uint32_t>(kObjPanelRowH)}};  // 有滚动条时行宽让出 9px
            const bool hovered =
                app.gizmo.mouseX >= static_cast<float>(rowRect.offset.x) &&
                app.gizmo.mouseX <  static_cast<float>(rowRect.offset.x + static_cast<int32_t>(rowRect.extent.width)) &&
                app.gizmo.mouseY >= static_cast<float>(rowRect.offset.y) &&
                app.gizmo.mouseY <  static_cast<float>(rowRect.offset.y + static_cast<int32_t>(rowRect.extent.height));
            if (i == app.scene.selectedObject) {
                DrawPanel(app, {rowRect, selColor, 0.0f, kNoBorder, 0.0f});  // 选中：蓝色整行（无圆角）
                // 覆盖加粗白字（Blender 选中感：蓝条 + 加粗白字）
                if (app.ui.objNameHighlight.set != VK_NULL_HANDLE && app.ui.objNameHighlight.w > 0) {
                    const int32_t hx = rowRect.offset.x + 4;
                    const int32_t hy = rowRect.offset.y + (rowRect.extent.height - app.ui.objNameHighlight.h) / 2;
                    const int32_t hw = std::min<int32_t>(app.ui.objNameHighlight.w, static_cast<int32_t>(rowRect.extent.width) - 8);
                    if (hw > 8) {
                        DrawIcon(app, {{hx, hy}, {static_cast<uint32_t>(hw), static_cast<uint32_t>(app.ui.objNameHighlight.h)}},
                                 VkClearColorValue{{1.0f, 1.0f, 1.0f, 1.0f}}, app.ui.objNameHighlight.set);
                    }
                }
            } else if (hovered) {
                VkClearColorValue hc;
                for (int c = 0; c < 3; ++c) hc.float32[c] = std::min(1.0f, app.ui.buttonTheme.normal[c] * 1.15f + 0.05f);  // 淡亮
                hc.float32[3] = 1.0f;
                DrawPanel(app, {rowRect, hc, 0.0f, kNoBorder, 0.0f});  // 悬停：淡亮
            }
        }
 // 物体名字列表（纹理仅含可见 kObjMaxRows 行，滚动时由 UpdateObjectLabels 重建）
        if (app.ui.objNameLabel.set != VK_NULL_HANDLE && app.ui.objNameLabel.w > 0) {
            const VkRect2D listRect{{px, listY},
                                    {static_cast<uint32_t>(app.ui.objNameLabel.w),
                                     static_cast<uint32_t>(app.ui.objNameLabel.h)}};
            DrawIcon(app, listRect, VkClearColorValue{{1.0f, 1.0f, 1.0f, 1.0f}}, app.ui.objNameLabel.set);
        }
 // 滚动条（物体数 > kObjMaxRows 时右侧显示）：轨道 + 滑块
        if (maxScroll > 0) {
            const int trackW = 5;
            const int trackX = px + pw - trackW - 2;
            const VkClearColorValue trackCol{{0.18f, 0.19f, 0.22f, 1.0f}};
            const VkClearColorValue thumbCol{{0.42f, 0.45f, 0.52f, 1.0f}};
            DrawPanel(app, {{{trackX, listY}, {trackW, static_cast<uint32_t>(listH)}}, trackCol, 0.0f, kNoBorder, 0.0f});
            const int thumbH = std::max(18, listH * kObjMaxRows / nRows);
            const int slide = listH - thumbH;
            const int thumbY = listY + (maxScroll > 0 ? app.ui.objScroll * slide / maxScroll : 0);
            DrawPanel(app, {{{trackX, thumbY}, {trackW, static_cast<uint32_t>(thumbH)}}, thumbCol, 0.0f, kNoBorder, 0.0f});
        }
    }

    DrawMenu(app);

    // 选中物体 AABB 黄框（2D HUD；按文件分布规则放 ui3d.cpp：clamp 屏幕坐标避免大模型顶点延伸无穷远）
    // 右上角系统按钮（最小化/最大化/关闭，PS 风格；悬停高亮，关闭红底白叉）
    {
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineUI);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        for (int i = 0; i < 3; ++i) {
            const UiButton& s = app.ui.sysButtons[i];
            const ButtonState st = s.machine.state;
            // 关闭按钮悬停变红：优先用状态机 Hover/Pressed；本窗口自绘无边框+自定义拖拽，
            // 状态机 hover 不可靠时，用鼠标实时位置命中兜底，确保"鼠标移到关闭按钮上即变红"。
            const bool mouseOver = PointInButton(s, app.gizmo.mouseX, app.gizmo.mouseY);
            const bool closeHover = (i == 2) && (st == ButtonState::Hover || st == ButtonState::Pressed || mouseOver);
            if (closeHover) {
                // 关闭按钮悬停/按下：红底
                const VkClearColorValue red{{0.8f, 0.16f, 0.16f, 1.0f}};
                DrawPanel(app, {s.rect, red, 0.0f});
            } else if (st == ButtonState::Pressed) {
                // 按下状态：颜色变深（×0.55，视觉反馈）
                VkClearColorValue dark{};
                for (int c = 0; c < 4; ++c) dark.float32[c] = s.color[c] * 0.55f;
                DrawPanel(app, {s.rect, dark, 0.0f});
            } else if (st == ButtonState::Hover) {
                // 悬停：浅底
                VkClearColorValue fill{};
                for (int c = 0; c < 4; ++c) fill.float32[c] = s.color[c];
                DrawPanel(app, {s.rect, fill, 0.0f});
            }
            if (app.vk.textPipeline != VK_NULL_HANDLE) {
                const float cx = static_cast<float>(s.rect.offset.x) + s.rect.extent.width * 0.5f;
                const float cy = static_cast<float>(s.rect.offset.y) + s.rect.extent.height * 0.5f;
                const VkClearColorValue col = (i == 2 && closeHover)
                    ? VkClearColorValue{{1.0f, 1.0f, 1.0f, 1.0f}}
                    : (st == ButtonState::Pressed)
                        ? VkClearColorValue{{0.65f, 0.65f, 0.65f, 1.0f}}  // 按下深灰
                        : VkClearColorValue{{0.85f, 0.85f, 0.85f, 1.0f}};
                // Round296：右上角三图标整体缩小、线更细（halfWidth 1.2→0.7）；
                // 最大化按钮在已最大化时显示"还原"图标（双矩形）以表明当前状态。
                const float lw = 0.7f;  // 细线
                if (i == 2) {                       // 关闭：X（半幅 5.0 ≈10px，与最大化方框 r=4.0 协调，不再偏大）
                    const float gs = 5.0f;
                    DrawLine(app, s.rect, cx - gs, cy - gs, cx + gs, cy + gs, col, lw);
                    DrawLine(app, s.rect, cx - gs, cy + gs, cx + gs, cy - gs, col, lw);
                } else if (i == 1) {                // 最大化按钮：手绘图标（白渲染通道）；已最大化显示"还原"双矩形，否则单矩形
                    const float r = 4.0f;           // 半幅（#4：方框半幅 5.0→4.0，细线）
                    const float o = 1.6f;           // 还原图标两矩形偏移量
                    if (app.ui.maximized) {
                        // 还原图标：两个重叠矩形（左下在前、右上在后）
                        DrawLine(app, s.rect, cx + o - r, cy - o - r, cx + o + r, cy - o - r, col, lw);
                        DrawLine(app, s.rect, cx + o - r, cy - o + r, cx + o + r, cy - o + r, col, lw);
                        DrawLine(app, s.rect, cx + o - r, cy - o - r, cx + o - r, cy - o + r, col, lw);
                        DrawLine(app, s.rect, cx + o + r, cy - o - r, cx + o + r, cy - o + r, col, lw);
                        DrawLine(app, s.rect, cx - o - r, cy + o - r, cx - o + r, cy + o - r, col, lw);
                        DrawLine(app, s.rect, cx - o - r, cy + o + r, cx - o + r, cy + o + r, col, lw);
                        DrawLine(app, s.rect, cx - o - r, cy + o - r, cx - o - r, cy + o + r, col, lw);
                        DrawLine(app, s.rect, cx - o + r, cy + o - r, cx - o + r, cy + o + r, col, lw);
                    } else {
                        // 最大化图标：单矩形
                        DrawLine(app, s.rect, cx - r, cy - r, cx + r, cy - r, col, lw);
                        DrawLine(app, s.rect, cx - r, cy + r, cx + r, cy + r, col, lw);
                        DrawLine(app, s.rect, cx - r, cy - r, cx - r, cy + r, col, lw);
                        DrawLine(app, s.rect, cx + r, cy - r, cx + r, cy + r, col, lw);
                    }
                } else {                            // 最小化：底部短线（垂直居中，更小）
                    DrawLine(app, s.rect, cx - 5.0f, cy, cx + 5.0f, cy, col, lw);
                }
            }
        }
    }
    DrawSelectionOutline(app, mvp, layout);
}

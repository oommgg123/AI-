// ============================================================================
//  ui3d.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"
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
    const uint32_t bodyTop = topH;
    const uint32_t bodyBottom = (h >= bottomH) ? h - bottomH : 0;
    const uint32_t bodyH  = (bodyBottom > bodyTop) ? bodyBottom - bodyTop : 0;
    const uint32_t centerW = (w >= leftW + rightW) ? w - leftW - rightW : 0;

    Layout l{};
    l.top      = {{0, 0}, {w, topH}};
    l.left     = {{0, static_cast<int32_t>(bodyTop)}, {leftW, bodyH}};
    l.right    = {{static_cast<int32_t>(w - rightW), static_cast<int32_t>(bodyTop)},
                  {rightW, bodyH}};
    l.bottom   = {{0, static_cast<int32_t>(h - bottomH)}, {w, bottomH}};
    l.viewport = {{static_cast<int32_t>(leftW), static_cast<int32_t>(bodyTop)},
                  {centerW, bodyH}};
    return l;
}
int HitResizeDivider(const App& app, float mx, float my) {
    const Layout lay = ComputeLayout(app);
    constexpr float kTol = 6.0f;
    const float topY    = static_cast<float>(lay.top.extent.height);
    const float bottomY = static_cast<float>(lay.bottom.offset.y);
    const float leftX   = static_cast<float>(lay.left.extent.width);
    const float rightX  = static_cast<float>(lay.right.offset.x);
    if (my >= topY && my <= bottomY) {
 if (std::fabs(mx - leftX) <= kTol) return 1; // 左栏右缘（垂直）
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
    const int ew = 320, eh = 26;
    const int ex = (static_cast<int>(app.vk.swapchainExtent.width) - ew) / 2;
    const int ey = static_cast<int>(app.ui.panelTopH) + 10;
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
void DrawIcon(App& app, const VkRect2D& iconRect, VkClearColorValue color, VkDescriptorSet set) {
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
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
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
void UploadLabelRgba(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h) {
    lt.w = lt.h = 0;
    if (w <= 0 || h <= 0 || rgba.empty()) return;
 // 等上一帧完成（inFlightFence，#215）：上一帧可能仍引用旧 image/view，销毁前须确保 GPU 不再使用——只等单帧栅栏，不再 vkDeviceWaitIdle 全设备停顿
    vkWaitForFences(app.vk.device, 1, &app.vk.inFlightFence, VK_TRUE, 2000000000ull);
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
bool RasterizeNameList(const std::vector<std::wstring>& names, int width, int rowH,
                              std::vector<uint8_t>& rgba, int& outW, int& outH) {
    const int h = static_cast<int>(names.size()) * rowH;
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
    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    for (size_t i = 0; i < names.size(); ++i) {
        RECT rc{2, static_cast<int>(i) * rowH, width - 2, static_cast<int>(i) * rowH + rowH};
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
    std::vector<std::wstring> names;
    std::wstring key;
    for (const auto& o : app.scene.objects) {
        names.push_back(o.name);
        key += std::to_wstring(o.name.size()) + L":" + o.name + L"\n";
    }
    key += L"sel=" + std::to_wstring(app.scene.selectedObject);
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
void DrawLogicBar(App& app, const Layout& layout) {
    const uint32_t w = app.vk.swapchainExtent.width;
    const uint32_t h = app.vk.swapchainExtent.height;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
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

 // 仅触碰（悬停）可拖拽窗口边缘才高亮——顶栏(div 0)不可拖不高亮；
 // 悬停在左栏右缘/右栏左缘/底栏上缘（HitResizeDivider 返回 1/2/3）→ 该边缘画 2px 黄色亮线
    {
        const int div = HitResizeDivider(app, app.gizmo.mouseX, app.gizmo.mouseY);
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

 // 顶栏按钮高度随栏位变化（上下各留 4px），图标/字符按按钮中心自动居中（图标上限 48px）
    {
        const int32_t topH = static_cast<int32_t>(layout.top.extent.height);
        if (topH > 0) {
            for (auto& b : app.ui.buttons) {
 if (b.rect.offset.y >= static_cast<int32_t>(kTopBarHeight)) continue; // 仅顶栏按钮
 const int32_t bh = std::max(20, topH - 8); // 上下各留 4px；最小 20
                b.rect.offset.y = static_cast<int32_t>((topH - bh) / 2);
                b.rect.extent.height = static_cast<uint32_t>(bh);
            }
        }
    }

    for (size_t bi = 0; bi < app.ui.buttons.size(); ++bi) {
        const UiButton& btn = app.ui.buttons[bi];
        VkClearColorValue fill{};
        VkClearColorValue border{};
        for (int i = 0; i < 4; ++i) {
            fill.float32[i] = btn.color[i];
            border.float32[i] = btn.border[i];
        }
 // 变换模式按钮（顶栏后 3 个=移动/旋转/缩放）——当前 gizmoMode 激活提亮（视觉反馈）
        if (app.ui.buttons.size() >= 3 && bi >= app.ui.buttons.size() - 3 &&
            static_cast<int>(bi - (app.ui.buttons.size() - 3)) == app.ui.gizmoMode) {
            for (int c = 0; c < 3; ++c)
                fill.float32[c] = std::min(1.0f, fill.float32[c] * 1.35f + 0.1f);
        }
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
        DrawPanel(app, {btn.rect, fill, btn.radius, border});
 const float iconSize = std::min(static_cast<float>(btn.rect.extent.height) - 6.0f, 48.0f); // 图标上限 48px
        const float cx = static_cast<float>(btn.rect.offset.x) + btn.rect.extent.width * 0.5f;
        const float cy = static_cast<float>(btn.rect.offset.y) + btn.rect.extent.height * 0.5f;
        const VkRect2D iconRect{
            {static_cast<int32_t>(cx - iconSize * 0.5f), static_cast<int32_t>(cy - iconSize * 0.5f)},
            {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
        if (app.vk.textPipeline != VK_NULL_HANDLE) {
 // 顶栏后 3 个按钮（变换模式）= 4/5/6 图标；其余按 btn.icon 选描述集
            VkDescriptorSet set;
            if (app.ui.buttons.size() >= 3 && bi >= app.ui.buttons.size() - 3) {
                const int ti = static_cast<int>(bi - (app.ui.buttons.size() - 3));
                set = app.ui.transformIcons[ti].valid ? app.ui.transformIcons[ti].set : app.vk.textDescriptorSet;
            } else {
                set = (btn.icon == 1) ? app.vk.penDescriptorSet : app.vk.textDescriptorSet;
            }
            DrawIcon(app, iconRect, kButtonIconColor, set);
        }
    }

    {
        const VkRect2D btnBar{{0, 0}, {w, h}};
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &vertexOffset);
 // 顶栏分隔线统一样式修正：编辑按钮右侧分割线与 x=49 灰线同长同色
        constexpr float kTopDivY0 = 8.0f, kTopDivY1 = 28.0f;
        const VkClearColorValue kTopDivColor{{0.5f, 0.5f, 0.5f, 1.0f}};
        DrawLine(app, btnBar, 49.0f, kTopDivY0, 49.0f, kTopDivY1, kTopDivColor, 1.0f);
        if (app.ui.editDividerX > 0 && w >= static_cast<uint32_t>(app.ui.editDividerX) + 4) {
            DrawLine(app, btnBar, static_cast<float>(app.ui.editDividerX), kTopDivY0,
                     static_cast<float>(app.ui.editDividerX), kTopDivY1, kTopDivColor, 1.0f);
        }
    }

 if ((g_importProgress >= 0 && g_importProgress < 100) || g_importUploading.load() == 1) { // GPU 上传阶段进度条保持显示
        const VkRect2D& vp = layout.viewport;
        if (vp.extent.width > 0 && vp.extent.height > 0) {
            const uint64_t nowMs = GetTickCount64();
            const float bw = std::min(360.0f, static_cast<float>(vp.extent.width) * 0.5f);
            const float bh = 6.0f;
            const float bx = static_cast<float>(vp.offset.x) + (static_cast<float>(vp.extent.width) - bw) * 0.5f;
            const float by = static_cast<float>(vp.offset.y) + static_cast<float>(vp.extent.height) - 48.0f;
            const float realProg = static_cast<float>(g_importProgress.load());
            app.ui.importDisplayProg = std::max(app.ui.importDisplayProg, realProg);
            app.ui.importDisplayProg += (realProg - app.ui.importDisplayProg) * 0.2f;
            const float prog = std::clamp(app.ui.importDisplayProg, 0.0f, 100.0f);
 VkClearColorValue bgCol = ThemeColor(ui::g_theme.slider.track, 0.85f); // 进度条轨道底色
 VkClearColorValue fillCol = ThemeColor(ui::g_theme.slider.fill); // 进度条填充（accent）
            const VkRect2D barRect{{static_cast<int32_t>(bx), static_cast<int32_t>(by)},
                                   {static_cast<uint32_t>(bw), static_cast<uint32_t>(bh)}};
            DrawPanel(app, {barRect, bgCol, 3.0f});
            if (prog > 1.0f) {
                const float fillW = bw * prog / 100.0f;
                const VkRect2D fillRect{{static_cast<int32_t>(bx), static_cast<int32_t>(by)},
                                        {static_cast<uint32_t>(fillW), static_cast<uint32_t>(bh)}};
                DrawPanel(app, {fillRect, fillCol, 3.0f});
            }
            {
                const float cx = bx - 18.0f;
                const float cy = by + bh * 0.5f;
                const float rad = 8.0f;
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
                                    segCol, 2.2f);
                }
            }
 // GPU 上传阶段提示文字（进度条保持满条，真正完成才消失）
            if (g_importUploading.load() == 1) {
                const wchar_t* msg = L"正在上传渲染数据…";
                if (app.ui.importUpLabel.text != msg) {
                    std::vector<uint8_t> rgba;
                    int tw = 0, th = 0;
                    if (RasterizeText(msg, 13, 2, L"Segoe UI", rgba, tw, th))
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
            vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
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
                vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
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

 // 右侧物体显示栏多物体名称列表，Blender 风格；点击行选中
 // 跟随可调右栏布局（layout 为 ComputeLayout 单一来源）
    if (layout.right.extent.width >= 40 && layout.right.extent.height >= 100) {
        const int px = layout.right.offset.x + kObjPanelPad;
        const int py = layout.right.offset.y + kObjPanelPad;
        const int pw = static_cast<int>(layout.right.extent.width) - 2 * kObjPanelPad;
 const int ph = 250; // 物体栏高度÷2（原 500）
        const VkClearColorValue panelFill = kPanelColor;
        const VkClearColorValue whiteBorder = {{1.0f, 1.0f, 1.0f, 1.0f}};
        const VkClearColorValue selColor = ThemeColor(ui::g_theme.list.selected);
        vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipeline);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.vertexBuffer, &off);
        DrawPanel(app, {{{px - kObjPanelPad, py - kObjPanelPad},
                         {static_cast<uint32_t>(pw + 2 * kObjPanelPad), static_cast<uint32_t>(ph)}},
 panelFill, 0.0f, whiteBorder, 1.0f}); // 模型列表面板无圆角
 // 物体栏行级按钮化——每行按按钮渲染（按钮主题色 normal + 悬停提亮 + 选中高亮，圆角行）
        const int nRows = static_cast<int>(app.scene.objects.size());
        for (int i = 0; i < nRows; ++i) {
            const VkRect2D rowRect{{px, py + i * kObjPanelRowH},
                                   {static_cast<uint32_t>(pw), static_cast<uint32_t>(kObjPanelRowH)}};
            const bool hovered =
                app.gizmo.mouseX >= static_cast<float>(rowRect.offset.x) &&
                app.gizmo.mouseX <  static_cast<float>(rowRect.offset.x + static_cast<int32_t>(rowRect.extent.width)) &&
                app.gizmo.mouseY >= static_cast<float>(rowRect.offset.y) &&
                app.gizmo.mouseY <  static_cast<float>(rowRect.offset.y + static_cast<int32_t>(rowRect.extent.height));
            VkClearColorValue rc;
            for (int c = 0; c < 4; ++c) rc.float32[c] = app.ui.buttonTheme.normal[c];
 if (i == app.scene.selectedObject) rc = selColor; // 选中高亮（主题选中色）
 else if (hovered) for (int c = 0; c < 3; ++c) rc.float32[c] = std::min(1.0f, app.ui.buttonTheme.normal[c] * 1.5f + 0.08f); // 悬停提亮
 DrawPanel(app, {rowRect, rc, 4.0f, rc, 0.0f}); // 圆角行（4px）
        }
 // 全部物体名字列表（DrawIcon 内部切换 textPipeline；透明底白字，混合显示）
        if (app.ui.objNameLabel.set != VK_NULL_HANDLE && app.ui.objNameLabel.w > 0) {
            const VkRect2D listRect{{px, py},
                                    {static_cast<uint32_t>(app.ui.objNameLabel.w),
                                     static_cast<uint32_t>(app.ui.objNameLabel.h)}};
            DrawIcon(app, listRect, VkClearColorValue{{0.88f, 0.88f, 0.88f, 1.0f}}, app.ui.objNameLabel.set);
        }
    }

    DrawMenu(app);
}

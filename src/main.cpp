// ============================================================================
//
// 现代化与兼容性：
//       * 兼容路径：传统 render pass + framebuffer（Vulkan 1.0 core）
//     —— 运行时探测设备能力，老显卡 / 核显不支持动态渲染时自动回退
//   - 实例 apiVersion 申请 1.0（最大兼容老驱动），KHR 扩展入口运行时加载
//   - 呈现模式优先 MAILBOX（低延迟），不支持则回退 FIFO
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
#include "mc_blocks.h"     // 我的世界通用方块库 + 图集 + 合并网格（Round193）
#include "import_pipeline.h"
#include "import_window.h"
#include "app.h"
#include "ui_button.h"   // 共享 GDI 按钮渲染管线（UpdateButtonColor / PointInButton / DrawGdiButton）
#include "ui_presets.h"  // 统一控件预设体系：2D UI 面板/描边/图标颜色从 ui::g_theme 取值
#include "resource.h"    // Round277：嵌入的球按钮图标 RCDATA 资源 ID

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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

#include <rounded_rect.vert.inc>
#include <rounded_rect.frag.inc>
#include <unified3d.vert.inc>
#include <unified3d.frag.inc>
#include <grid.vert.inc>
#include <grid.frag.inc>
#include <axis3d.vert.inc>
#include <axis3d.frag.inc>
#include <line3d.vert.inc>
#include <line3d.frag.inc>
#include <text.vert.inc>
#include <text.frag.inc>
#include <fxaa.vert.inc>
#include <fxaa.frag.inc>

#include <gear_png.inc>
#include <pen_png.inc>
#include <import_png.inc>
#include <export_png.inc>

using std::uint32_t;

// ---- 3D 模型存储区（用户本轮：MC 加载完成后先存这里，不直接渲染到视口）----
// SceneObject 已通过 model_import.h 引入
struct StoredModel { SceneObject obj; std::wstring label; };
static std::vector<StoredModel> g_mcModelStore;

// 窗口标题：**普通宽字符数组**（用户 173 轮"窗口显示名称为什么只有 1 个字符"）——
// 旧实现 `std::wstring_view` 的 .data() 传入 CreateWindowExW 在 MinGW -fshort-wchar
// 下可能被截断为 1 字符；数组形式保证完整 UTF-16 传递
const wchar_t kWindowTitle[] = L"awa";
constexpr std::string_view kAppName = "awa";
// 版本号 = 单一来源（用户 179 轮：不硬编码）——由 CMakeLists project() VERSION 生成
// version_gen.h（VKB_VERSION_STR）；更新版本只改 CMakeLists 顶部 VERSION
#include "version_gen.h"
const wchar_t kVersionW[] = L"" VKB_VERSION_STR "";
// 软件著作人（用户 173 轮要求）
const wchar_t kAuthorW[]  = L"大章鱼用ai做的";
constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

// ---- 界面布局 ----
// Round358：kTopBarHeight / kSideBarWidth / kBottomBarHeight 已移至 app.h（App 默认值 + 拖拽最小限制共用）
constexpr float kLineWidth = 1.0f;
// 右侧物体列表（Round252：Blender 风格多物体编辑栏）行高 / 面板内边距（绘制与点击命中共用）
constexpr int kObjPanelRowH = 22;
constexpr int kObjPanelPad  = 8;

// 从 ui::g_theme 预设 COLORREF 转 Vulkan clear color（仅 2D UI 用；3D 视口/轴/gizmo 保持原样）
VkClearColorValue ThemeColor(COLORREF c, float a = 1.0f) {
    const ui::FloatColor f = ui::ToFloat(c, a);
    VkClearColorValue v{};
    v.float32[0] = f.r; v.float32[1] = f.g; v.float32[2] = f.b; v.float32[3] = f.a;
    return v;
}

// ---- 颜色方案（2D UI 从 ui::g_theme 预设取值；3D 视口/轴/gizmo 颜色保持原样，勿动）----
constexpr VkClearColorValue kBackgroundColor = {{0.12f, 0.12f, 0.12f, 1.0f}};   // 3D 视口背景
const VkClearColorValue      kPanelColor      = ThemeColor(ui::g_theme.palette.bg);         // 顶栏/侧栏/面板背景
constexpr VkClearColorValue kViewportColor   = {{0.14f, 0.14f, 0.14f, 1.0f}};   // 3D 视口延伸背景
const VkClearColorValue      kBorderColor     = ThemeColor(ui::g_theme.palette.panelBorder); // 面板/按钮描边
const VkClearColorValue      kButtonIconColor = ThemeColor(ui::g_theme.palette.text);        // 图标/按钮文字

// ---- 圆角 ----
constexpr float kCornerRadius = 6.0f;

// ---- 3D 视口右上角坐标轴指示器（真正的纯 3D 渲染） ----
constexpr float kGizmoViewportSize = 104.0f;
constexpr float kGizmoMargin = 56.0f;
constexpr float kGizmoHalfPx = 30.0f;
constexpr float kGizmoLabelSize = 9.0f;
constexpr VkClearColorValue kAxisXColor = {{0.95f, 0.30f, 0.30f, 1.0f}};
constexpr VkClearColorValue kAxisYColor = {{0.30f, 0.95f, 0.40f, 1.0f}};
constexpr VkClearColorValue kAxisZColor = {{0.35f, 0.55f, 1.00f, 1.0f}};
constexpr VkClearColorValue kCrosshairColor = {{0.36f, 0.36f, 0.40f, 1.0f}};

// ---- 3D 世界内容 ----
constexpr float kRenderDistance = 10000.0f;

// ---------------------------------------------------------------------------
// 错误收集：初始化任一环节失败即记录原因，由入口统一弹窗
// ---------------------------------------------------------------------------
std::string g_error;

const char* g_stage = "启动前";

void SetError(const std::string& msg) {
    if (g_error.empty()) {
        g_error = msg;
        VkbLog(("[seterror] " + msg).c_str());
    }
}

// 弹窗显示错误信息（UTF-8 → 宽字符，用 MessageBoxW 显示，避免 MessageBoxA 把
// UTF-8 中文按系统 ANSI 代码页解释成乱码——"vulkan(乱码)"报错的根因）
void ShowErrorBox(const char* utf8Msg) {
    wchar_t wmsg[4096];
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8Msg, -1, wmsg, 4096);
    if (n > 0) MessageBoxW(nullptr, wmsg, L"awa", MB_ICONERROR | MB_OK);
}

#define VKB_TRY(expr)                                                         \
    do {                                                                      \
        if (VkResult _vkb_res = (expr); _vkb_res != VK_SUCCESS) {             \
            SetError(std::string("Vulkan 调用失败: ") + #expr +               \
                     " (VkResult=" + std::to_string(static_cast<int>(_vkb_res)) + ")"); \
            return false;                                                     \
        }                                                                     \
    } while (0)

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
#ifdef VKB_ENABLE_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    OutputDebugStringA(data->pMessage);
    OutputDebugStringA("\n");
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SetError(std::string("验证层错误: ") + data->pMessage);
    }
    return VK_FALSE;
}
#endif

// ---------------------------------------------------------------------------
// （2026-08-19 用户要求"单独创建关于摄像机的 cpp 文件"；含预设视角 + 截图接口）
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void MatMul4(const float a[16], const float b[16], float out[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = s;
        }
    }
}

// Round296：物体 model 矩阵 = 平移 × 旋转（欧拉角，度，ZYX 顺序） × 缩放
void BuildModelMatrix(const SceneObject& o, float out[16]) {
    const float a = o.rx * 3.14159265f / 180.0f;
    const float b = o.ry * 3.14159265f / 180.0f;
    const float c = o.rz * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    const float cb = std::cos(b), sb = std::sin(b);
    const float cc = std::cos(c), sc = std::sin(c);
    // 绕轴旋转矩阵（列主序 4x4）
    const float Rx[16] = {1,0,0,0,  0,ca,sa,0,  0,-sa,ca,0,  0,0,0,1};
    const float Ry[16] = {cb,0,-sb,0, 0,1,0,0,  sb,0,cb,0,  0,0,0,1};
    const float Rz[16] = {cc,sc,0,0,  -sc,cc,0,0,  0,0,1,0,  0,0,0,1};
    float tmp[16], R[16];
    MatMul4(Ry, Rx, tmp);
    MatMul4(Rz, tmp, R);          // R = Rz·Ry·Rx
    const float S[16] = {o.sx,0,0,0, 0,o.sy,0,0, 0,0,o.sz,0, 0,0,0,1};
    float RS[16];
    MatMul4(R, S, RS);          // RS = R·S（Round356：缩放在旋转前 → 本地空间缩放，沿物体本地方向生效）
    const float T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, o.tx,o.ty,o.tz,1};
    MatMul4(T, RS, out);        // out = T·R·S（缩放本地化：先缩放后旋转，缩放随物体朝向改变）
}

// Round355：由本地欧拉角构造旋转矩阵 R=Rz·Ry·Rx（与 BuildModelMatrix 同序，列主序）。
// 供世界轴旋转（把世界旋转 premultiply 回 Euler）使用。
static void BuildRotFromEuler(float rx, float ry, float rz, float R[16]) {
    const float a = rx * 3.14159265f / 180.0f;
    const float b = ry * 3.14159265f / 180.0f;
    const float c = rz * 3.14159265f / 180.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    const float cb = std::cos(b), sb = std::sin(b);
    const float cc = std::cos(c), sc = std::sin(c);
    const float Rx[16] = {1,0,0,0, 0,ca,sa,0, 0,-sa,ca,0, 0,0,0,1};
    const float Ry[16] = {cb,0,-sb,0, 0,1,0,0, sb,0,cb,0, 0,0,0,1};
    const float Rz[16] = {cc,sc,0,0, -sc,cc,0,0, 0,0,1,0, 0,0,0,1};
    float tmp[16];
    MatMul4(Ry, Rx, tmp);
    MatMul4(Rz, tmp, R);          // R = Rz·Ry·Rx（列主序）
}
// Round355：绕世界轴（0=X/1=Y/2=Z）旋转 deg 度的世界旋转矩阵（premultiply 到现有旋转 = 世界空间旋转）
static void MakeWorldRot(int axis, float deg, float R[16]) {
    const float r = deg * 3.14159265f / 180.0f;
    const float c = std::cos(r), s = std::sin(r);
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if (axis == 0) {        // 绕世界 X
        m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
    } else if (axis == 1) { // 绕世界 Y
        m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
    } else {                 // 绕世界 Z
        m[0] = c; m[1] = s; m[4] = -s; m[5] = c;
    }
    for (int i = 0; i < 16; ++i) R[i] = m[i];
}
// Round355：由 R=Rz·Ry·Rx（与 BuildRotFromEuler 同序）反解本地欧拉角 rx/ry/rz（度）。
// 列主序 R 的元素：R[2]=-sin(ry)，R[0]=cos(ry)cos(rz)，R[1]=cos(ry)sin(rz)，R[6]=cos(ry)sin(rx)，R[10]=cos(ry)cos(rx)
static void EulerFromR(const float R[16], float& rx, float& ry, float& rz) {
    const float sy = -R[2];                                   // R[2] = -sin(ry)
    float ryv = std::asin(std::max(-1.0f, std::min(1.0f, sy)));
    const float cy = std::cos(ryv);
    if (cy > 1e-6f) {
        rz = std::atan2(R[1], R[0]) * 180.0f / 3.14159265f;   // atan2(sin(rz),cos(rz))
        rx = std::atan2(R[6], R[10]) * 180.0f / 3.14159265f;  // atan2(sin(rx),cos(rx))
    } else {                                                  // 万向锁（ry≈±90°）：固定 rz=0，由残余解 rx
        rz = 0.0f;
        rx = std::atan2(R[4], R[5]) * 180.0f / 3.14159265f;
    }
    ry = ryv * 180.0f / 3.14159265f;
}

// Round296：旋转选中物体（绕世界轴，deg 为度数；记录撤销）。axis: 'X'/'Y'/'Z'
void RotateSelectedObject(App& app, char axis, float deg) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    SceneObject& so = app.objects[app.selectedObject];
    App::UndoEntry e;
    e.op = App::UndoOp::Rotate;
    e.index = app.selectedObject;
    e.oldRx = so.rx; e.oldRy = so.ry; e.oldRz = so.rz;
    switch (axis) {
        case 'X': so.rx += deg; break;
        case 'Y': so.ry += deg; break;
        case 'Z': so.rz += deg; break;
        default: return;
    }
    e.newRx = so.rx; e.newRy = so.ry; e.newRz = so.rz;
    PushUndo(app, e);
}

// Round296：缩放选中物体（等比 factor；记录撤销）
void ScaleSelectedObject(App& app, float factor) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    if (factor <= 0.0f) return;
    SceneObject& so = app.objects[app.selectedObject];
    App::UndoEntry e;
    e.op = App::UndoOp::Scale;
    e.index = app.selectedObject;
    e.oldSx = so.sx; e.oldSy = so.sy; e.oldSz = so.sz;
    so.sx *= factor; so.sy *= factor; so.sz *= factor;
    e.newSx = so.sx; e.newSy = so.sy; e.newSz = so.sz;
    PushUndo(app, e);
}

void OrthoMatrix(float halfSize, float out[16]) {
    std::fill(out, out + 16, 0.0f);
    out[0] = 1.0f / halfSize;
    out[5] = -1.0f / halfSize;
    out[10] = 1.0f;
    out[15] = 1.0f;
}

// 返回 false 表示点在裁剪面后方（w <= 0），不可见
bool ProjectToViewport(const float mvp[16], const VkViewport& viewport,
                       float px, float py, float pz, float& sx, float& sy) {
    const float x = mvp[0] * px + mvp[4] * py + mvp[8] * pz + mvp[12];
    const float y = mvp[1] * px + mvp[5] * py + mvp[9] * pz + mvp[13];
    const float w = mvp[3] * px + mvp[7] * py + mvp[11] * pz + mvp[15];
    if (w <= 1e-6f) return false;
    const float nx = x / w;
    const float ny = y / w;
    sx = viewport.x + (nx * 0.5f + 0.5f) * viewport.width;
    sy = viewport.y + (ny * 0.5f + 0.5f) * viewport.height;
    return true;
}

// 顶点含颜色（用户 156 轮否决瘦身：以后要做带颜色的光照渲染，颜色必须留在顶点里）
// 结构与 SceneObject 已移至 model_import.h（用户 176 轮：STL/glTF/FBX 共享）

// 颜色位深模式（用户 159 轮）：0=无颜色 1=16bit 2=8bit 3=4bit 4=1bit → 顶点 stride
static const uint32_t kColorStride[5] = {24u, 32u, 28u, 26u, 25u};

// float → IEEE 754 半精度（16bit 颜色用，0~1 范围足够精确）
static uint16_t FloatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
    const uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF) return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
    if (exp >= 0x1F) return static_cast<uint16_t>(sign | 0x7C00u);
    if (exp <= 0) {
        if (exp < -10) return static_cast<uint16_t>(sign);
        const uint32_t m = mant | 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exp);
        uint32_t h = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1u);
        if (rem > (1u << (shift - 1u)) || (rem == (1u << (shift - 1u)) && (h & 1u))) ++h;
        return static_cast<uint16_t>(sign | h);
    }
    uint32_t h = sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13);
    const uint32_t rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
    return static_cast<uint16_t>(h);
}

struct Push3D {
    float mvp[16];
    float mode;
    float gridRadius;
    float modelRadius;
    float camXZ[2];
    float objXZ[2];
    float hasColor;    // 0=无颜色（shader 用常量色），1=有颜色（用 vColor）
    float fadeDisable; // 1=禁用远处渐隐（MC 模型要完全不透明）
};
static_assert(sizeof(Push3D) == 100, "Push3D 布局需与 unified3d shader 一致");

struct Axis3DPush {
    float mvp[16];
    float pointA[4];
    float pointB[4];
    float params[4];
    float color[4];
};
static_assert(sizeof(Axis3DPush) == 128, "Axis3DPush 布局需与 axis3d shader 一致");

// ===========================================================================
// ===========================================================================
// SceneObject 结构定义在 model_import.h（用户 176 轮：OBJ/STL/glTF/FBX 共享）

wchar_t g_startupObjPath[MAX_PATH] = L"";

// 渲染开关（用户 166 轮）：
// g_useQuads：true=四边形面渲染（性能观感兼具，默认）；false=三角面（兼容）
// g_showNormals：true=渲染法线线（默认关闭，保留接口）
// g_swapZtoY：true=OBJ Z-up → Y-up 旋转（默认；多数 OBJ 文件是 Z-up，旋转后模型正立）
//               false=不旋转（适用于已经是 Y-up 的模型，避免旋转 90° 导致模型倾倒）
bool g_useQuads = true;
bool g_showNormals = false;
bool g_swapZtoY = true;



// 4x4 列主序矩阵求逆（高斯-约当消元）；不可逆返回 false
bool Mat4Inverse(const float m[16], float out[16]) {
    float a[16];
    std::memcpy(a, m, sizeof(a));
    std::fill(out, out + 16, 0.0f);
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        float maxV = std::fabs(a[col * 4 + col]);
        for (int r = col + 1; r < 4; ++r) {
            const float v = std::fabs(a[col * 4 + r]);
            if (v > maxV) { maxV = v; pivot = r; }
        }
        if (maxV < 1e-12f) return false;
        if (pivot != col) {
            for (int c = 0; c < 4; ++c) {
                std::swap(a[c * 4 + col], a[c * 4 + pivot]);
                std::swap(out[c * 4 + col], out[c * 4 + pivot]);
            }
        }
        const float inv = 1.0f / a[col * 4 + col];
        for (int c = 0; c < 4; ++c) { a[c * 4 + col] *= inv; out[c * 4 + col] *= inv; }
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const float f = a[col * 4 + r];
            if (std::fabs(f) < 1e-15f) continue;
            for (int c = 0; c < 4; ++c) {
                a[c * 4 + r] -= f * a[c * 4 + col];
                out[c * 4 + r] -= f * out[c * 4 + col];
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 与场景对象（立方体）一起渲染，**从坐标轴/gizmo 管线独立出来**（用户要求）。
// ---------------------------------------------------------------------------

std::string TrimStr(const std::string& s) {
    const char* ws = " \t\r";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// 路径用窄字符 UTF-8 处理（绕开 CRT wchar_t 4 字节错位，与 LoadSceneObjects 同理）
ButtonTheme LoadButtonTheme() {
    ButtonTheme theme;
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

// ---------------------------------------------------------------------------
//   路径用窄字符 UTF-8 处理（绕开 CRT wchar_t 4 字节错位，与 LoadButtonTheme 同理）。
// ---------------------------------------------------------------------------
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

// UpdateButtonColor / PointInButton 已迁入 ui_button.cpp（共享 GDI 按钮渲染管线）

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
struct Layout {
    VkRect2D top;
    VkRect2D left;
    VkRect2D right;
    VkRect2D bottom;    // Round358：底部面板（默认高 150）
    VkRect2D viewport;
};

// Round358：布局单一来源——面板尺寸取自 App 可调字段（默认=最小，拖分隔线调整），视口吃剩余空间
Layout ComputeLayout(const App& app) {
    const uint32_t w = app.swapchainExtent.width;
    const uint32_t h = app.swapchainExtent.height;
    const uint32_t topH    = (h > app.panelTopH)    ? app.panelTopH    : 0;
    const uint32_t bottomH = (h > app.panelBottomH) ? app.panelBottomH : 0;
    const uint32_t leftW   = (w > app.panelLeftW)   ? app.panelLeftW   : 0;
    const uint32_t rightW  = (w > app.panelRightW)  ? app.panelRightW  : 0;
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

// Round364：命中可拖拽分隔线（1=左栏右缘 2=右栏左缘 3=底栏上缘；-1=无），整条线容差 ±6px。
// 顶栏(0)已取消上下缩放（固定 36px）；编号与机制保留，以后别处可恢复。分割线随布局（ComputeLayout）动态伸缩。
static int HitResizeDivider(const App& app, float mx, float my) {
    const Layout lay = ComputeLayout(app);
    constexpr float kTol = 6.0f;
    const float topY    = static_cast<float>(lay.top.extent.height);
    const float bottomY = static_cast<float>(lay.bottom.offset.y);
    const float leftX   = static_cast<float>(lay.left.extent.width);
    const float rightX  = static_cast<float>(lay.right.offset.x);
    if (my >= topY && my <= bottomY) {
        if (std::fabs(mx - leftX) <= kTol) return 1;   // 左栏右缘（垂直）
        if (std::fabs(mx - rightX) <= kTol) return 2;  // 右栏左缘（垂直）
    }
    if (std::fabs(my - bottomY) <= kTol) return 3;     // 底栏上缘（全宽）
    return -1;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
App* GetApp(HWND hwnd) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

inline float MouseX(LPARAM lParam) { return static_cast<float>(static_cast<int16_t>(LOWORD(lParam))); }
inline float MouseY(LPARAM lParam) { return static_cast<float>(static_cast<int16_t>(HIWORD(lParam))); }

void DrawFrame(App& app);
bool CreateVertexBuffer3D(App& app);
uint32_t FindMemoryType(const App& app, uint32_t typeFilter, VkMemoryPropertyFlags props);  // 定义在下方，三向标绘制前置声明

// ==================== 3D 物体选中/线框（Round237，Blender 风格）====================

// 射线-AABB 相交（slab 法），返回最近 t；无相交返回 -1
static float RayAABB(const float* o, const float* d,
                     float minx, float miny, float minz,
                     float maxx, float maxy, float maxz) {
    float tmin = 0.0f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float oi = o[i], di = d[i];
        const float mn = (i == 0 ? minx : (i == 1 ? miny : minz));
        const float mx = (i == 0 ? maxx : (i == 1 ? maxy : maxz));
        if (std::fabs(di) < 1e-9f) {
            if (oi < mn || oi > mx) return -1.0f;
        } else {
            float t1 = (mn - oi) / di, t2 = (mx - oi) / di;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    return tmin;
}

// 屏幕坐标（窗口像素）→ 拾取射线（o=相机位置，d=单位方向）；3D 视口外返回 false
static bool BuildViewRay(App& app, float mx, float my, float o[3], float d[3]) {
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return false;
    const float ndcX = (mx - static_cast<float>(vp.offset.x)) / static_cast<float>(vp.extent.width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (my - static_cast<float>(vp.offset.y)) / static_cast<float>(vp.extent.height) * 2.0f;
    float view[16];
    app.camera.ViewMatrix(view);
    const float s[3] = {view[0], view[4], view[8]};
    const float u[3] = {view[1], view[5], view[9]};
    const float f[3] = {-view[2], -view[6], -view[10]};
    const float tanF = std::tan(app.camera.fovDeg * 0.5f * 3.14159265f / 180.0f);
    const float aspect = static_cast<float>(vp.extent.width) / static_cast<float>(vp.extent.height);
    const float camX = ndcX * tanF * aspect;
    const float camY = ndcY * tanF;
    d[0] = s[0] * camX + u[0] * camY + f[0];
    d[1] = s[1] * camX + u[1] * camY + f[1];
    d[2] = s[2] * camX + u[2] * camY + f[2];
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dl < 1e-9f) return false;
    d[0] /= dl; d[1] /= dl; d[2] /= dl;
    o[0] = app.camera.position.x; o[1] = app.camera.position.y; o[2] = app.camera.position.z;
    return true;
}

// 射线-三角形相交（Möller–Trumbore），命中返回 t，未命中 false
static bool RayTriangle(const float* o, const float* d,
                        const float* a, const float* b, const float* c, float& t) {
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9f) return false;
    const float invDet = 1.0f / det;
    const float s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * invDet;
    if (tt < 0.0f) return false;
    t = tt;
    return true;
}

// 屏幕坐标（窗口像素）→ 拾取物体索引（Round253：按**鼠标点到的实际三角面**计算，
// AABB 预判剔除；无三角面的物体回退 AABB；最近命中 -1）
static int PickObjectAt(App& app, float mx, float my) {
    float o[3], d[3];
    if (!BuildViewRay(app, mx, my, o, d)) return -1;
    int best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < static_cast<int>(app.objects.size()); ++i) {
        const SceneObject& ob = app.objects[i];
        // 平移应用到射线（等价于把物体移回原点；沿射线参数 t 不变）
        const float ro[3] = {o[0] - ob.tx, o[1] - ob.ty, o[2] - ob.tz};
        // AABB 预判（快速剔除）
        if (RayAABB(ro, d, ob.boundsMin[0], ob.boundsMin[1], ob.boundsMin[2],
                    ob.boundsMax[0], ob.boundsMax[1], ob.boundsMax[2]) < 0.0f) continue;
        // 三角面精确拾取（鼠标实际点到的面）
        float tLocal = 1e30f;
        const auto& sv = ob.solidVerts;
        const auto& si = ob.solidIndices;
        if (!si.empty()) {
            for (size_t k = 0; k + 2 < si.size(); k += 3) {
                const VertexSolid& va = sv[si[k]];
                const VertexSolid& vb = sv[si[k + 1]];
                const VertexSolid& vc = sv[si[k + 2]];
                float tt = 0.0f;
                if (RayTriangle(ro, d, va.pos, vb.pos, vc.pos, tt) && tt < tLocal) tLocal = tt;
            }
        }
        if (tLocal >= 1e29f) {   // 无三角命中：纯线框/无面物体回退 AABB 最近点
            tLocal = RayAABB(ro, d, ob.boundsMin[0], ob.boundsMin[1], ob.boundsMin[2],
                             ob.boundsMax[0], ob.boundsMax[1], ob.boundsMax[2]);
            if (tLocal < 0.0f) continue;
        }
        if (tLocal < bestT) { bestT = tLocal; best = i; }
    }
    return best;
}

// ==================== 移动三向标（Round239，Blender 风格）====================

// 二维点到线段距离（px）
static float DistPointToSeg2D(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax, aby = by - ay;
    const float len2 = abx * abx + aby * aby;
    float t = 0.0f;
    if (len2 > 1e-9f) {
        t = ((px - ax) * abx + (py - ay) * aby) / len2;
        t = std::max(0.0f, std::min(1.0f, t));
    }
    const float cx = ax + abx * t, cy = ay + aby * t;
    const float dx = px - cx, dy = py - cy;
    return std::sqrt(dx * dx + dy * dy);
}

// 直线 L(t)=a+dir*t（dir 单位向量）与射线 R(s)=o+d*s 的最近点参数 t
static bool ClosestAxisParam(const float a[3], const float dir[3],
                             const float o[3], const float d[3], float& t) {
    const float B = dir[0] * d[0] + dir[1] * d[1] + dir[2] * d[2];
    const float C = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    const float w0[3] = {a[0] - o[0], a[1] - o[1], a[2] - o[2]};
    const float W = dir[0] * w0[0] + dir[1] * w0[1] + dir[2] * w0[2];
    const float V = d[0] * w0[0] + d[1] * w0[1] + d[2] * w0[2];
    const float denom = C - B * B;   // dir 单位 → A=1
    if (std::fabs(denom) < 1e-8f) { t = 0.0f; return false; }  // 视线与轴近平行：不稳定
    t = (V * B - W * C) / denom;
    return true;
}

// 选中物体世界枢轴（AABB 中心 + 平移）
static void GizmoPivot(const SceneObject& o, float out[3]) {
    out[0] = (o.boundsMin[0] + o.boundsMax[0]) * 0.5f + o.tx;
    out[1] = (o.boundsMin[1] + o.boundsMax[1]) * 0.5f + o.ty;
    out[2] = (o.boundsMin[2] + o.boundsMax[2]) * 0.5f + o.tz;
}

// 三向标轴长（Round245）：按"相机到枢轴距离"保持**恒定屏幕尺寸**——
// 模型变大不跟着变大；推拉缩放时随距离等比缩放（透视下屏幕长度恒定）
static float GizmoAxisLen(App& app, const float pivot[3], const VkRect2D& vp) {
    if (vp.extent.height <= 0) return 1.0f;
    const float dx = pivot[0] - app.camera.position.x;
    const float dy = pivot[1] - app.camera.position.y;
    const float dz = pivot[2] - app.camera.position.z;
    const float dist = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3f);
    const float fov = app.camera.fovDeg * 3.14159265f / 180.0f;
    const float worldPerPx = (2.0f * dist * std::tan(fov * 0.5f)) / static_cast<float>(vp.extent.height);
    constexpr float kTargetPx = 120.0f;  // 目标屏幕轴长（像素；用户 Round247"轴长和箭头变大2倍"：60→120）
    return kTargetPx * worldPerPx;
}

// 鼠标命中三向标轴（投影到屏幕后距线段 <14px）；返回轴索引 0/1/2，未命中 -1
static int PickGizmoAxisAt(App& app, float mx, float my) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return -1;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return -1;
    const float len = GizmoAxisLen(app, p, vp);
    float view[16], proj[16], mvp[16];
    const float aspect = static_cast<float>(vp.extent.width) / static_cast<float>(vp.extent.height);
    app.camera.ViewMatrix(view);
    app.camera.ProjectionMatrix(aspect, proj);
    MatMul4(proj, view, mvp);
    VkViewport vv{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                  static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）
    float best = 1e30f;
    int bestAxis = -1;
    for (int i = 0; i < 3; ++i) {
        const float tip[3] = {p[0] + dirs[i][0] * len, p[1] + dirs[i][1] * len, p[2] + dirs[i][2] * len};
        float sx1, sy1, sx2, sy2;
        if (!ProjectToViewport(mvp, vv, p[0], p[1], p[2], sx1, sy1)) continue;
        if (!ProjectToViewport(mvp, vv, tip[0], tip[1], tip[2], sx2, sy2)) continue;
        const float dist = DistPointToSeg2D(mx, my, sx1, sy1, sx2, sy2);
        if (dist < best) { best = dist; bestAxis = i; }
    }
    return (best <= 14.0f) ? bestAxis : -1;
}

// 射线 o+td 与平面 (n·(x-p)=0) 求交；返回交点（世界坐标）。平面与视线近平行返回 false
static bool RayPlane(const float o[3], const float d[3], const float n[3], const float p[3], float out[3]) {
    const float denom = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = (n[0] * (p[0] - o[0]) + n[1] * (p[1] - o[1]) + n[2] * (p[2] - o[2])) / denom;
    out[0] = o[0] + d[0] * t;
    out[1] = o[1] + d[1] * t;
    out[2] = o[2] + d[2] * t;
    return true;
}

// 命中三向标中心环（枢轴投影到屏幕，鼠标距 <24px）→ 自由拖拽入口（Blender：拖中心球任意移动）
static bool HitGizmoRingAt(App& app, float mx, float my) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return false;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return false;
    float view[16], proj[16], mvp[16];
    const float aspect = static_cast<float>(vp.extent.width) / static_cast<float>(vp.extent.height);
    app.camera.ViewMatrix(view);
    app.camera.ProjectionMatrix(aspect, proj);
    MatMul4(proj, view, mvp);
    VkViewport vv{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                  static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    float sx, sy;
    if (!ProjectToViewport(mvp, vv, p[0], p[1], p[2], sx, sy)) return false;
    const float dx = mx - sx, dy = my - sy;
    return (dx * dx + dy * dy) <= 24.0f * 24.0f;
}

// 启动中心环自由拖拽（Round260：Blender 拖中心球 → 视口平面任意方向移动）；
// 记录起点平移 + 初始视口平面交点（Round263：白边拖拽已删除，仅中心环使用）
static void StartFreeDrag(App& app, HWND hwnd, float mx, float my) {
    SceneObject& so = app.objects[app.selectedObject];
    app.gizmoDragMode = 3;
    app.gizmoDragging = true;
    app.gizmoStartTx = so.tx; app.gizmoStartTy = so.ty; app.gizmoStartTz = so.tz;
    GizmoPivot(so, app.gizmoPivot);
    // 记录初始交点：移动 = 每帧交点增量
    app.gizmoLastHitValid = false;
    float o[3], d[3];
    if (BuildViewRay(app, mx, my, o, d)) {
        float view[16];
        app.camera.ViewMatrix(view);
        const float n[3] = {-view[2], -view[6], -view[10]};   // 相机前向 = 平面法线（视口平面）
        if (RayPlane(o, d, n, app.gizmoPivot, app.gizmoLastHit)) app.gizmoLastHitValid = true;
    }
    app.mouseDragged = true;   // 按下即视为拖拽：松开时不重新拾取
    SetCapture(hwnd);
}

// 写一个 gizmo 顶点（pos+normal+color）
static void GizmoFillVert(VertexSolid& vv, const float* pos, const float* col) {
    vv.pos[0] = pos[0]; vv.pos[1] = pos[1]; vv.pos[2] = pos[2];
    vv.normal[0] = 0.0f; vv.normal[1] = 1.0f; vv.normal[2] = 0.0f;
    vv.color[0] = col[0]; vv.color[1] = col[1]; vv.color[2] = col[2]; vv.color[3] = 1.0f;
}

// Round354：给定轴方向 d，求两个与其垂直的单位基向量 u/w（用于锥头截面圆）
static void GizmoPerpBasis(const float d[3], float u[3], float w[3]) {
    float rx = 0.0f, ry = 1.0f, rz = 0.0f;          // 参考向量（优先 Y，与 d 近平行时改 X）
    if (std::fabs(d[1]) >= 0.9f) { rx = 1.0f; ry = 0.0f; rz = 0.0f; }
    u[0] = ry * d[2] - rz * d[1];
    u[1] = rz * d[0] - rx * d[2];
    u[2] = rx * d[1] - ry * d[0];
    float lu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (lu < 1e-6f) { u[0] = 1.0f; u[1] = 0.0f; u[2] = 0.0f; } else { u[0] /= lu; u[1] /= lu; u[2] /= lu; }
    w[0] = d[1] * u[2] - d[2] * u[1];
    w[1] = d[2] * u[0] - d[0] * u[2];
    w[2] = d[0] * u[1] - d[1] * u[0];
    float lw = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (lw < 1e-6f) { w[0] = 0.0f; w[1] = 1.0f; w[2] = 0.0f; } else { w[0] /= lw; w[1] /= lw; w[2] /= lw; }
}

// Round354：前向声明（定义见下方 DrawRotate 之后）——DrawMoveGizmo 在其前使用，避免顺序问题
static void GizmoFillCone(const float* tip, const float* base, float rad,
                          const float* u, const float* w,
                          VertexSolid* v, int& vi, int n, const float* col);

// 创建并绑定 HOST 可见顶点缓冲（惰性：首次创建复用；Round302：容量不足时销毁重建，
// 因旋转/缩放 gizmo 与移动三向标共用缓冲且顶点数不同）
static bool EnsureHostVtxBuffer(App& app, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size) {
    if (buf != VK_NULL_HANDLE) {
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, buf, &mr);
        if (mr.size >= size) return true;
        vkDestroyBuffer(app.device, buf, nullptr);
        vkFreeMemory(app.device, mem, nullptr);
        buf = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    }
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(app.device, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(app.device, buf, &mr);
    const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mi;
    if (vkAllocateMemory(app.device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(app.device, buf, mem, 0);
    return true;
}

// 绘制移动三向标（Round244：锥头**实体填充、完全不透明**）：3 条彩色轴箭头（X 红 / Y 绿 / Z 蓝），
// 无深度测试**渲染于最顶层始终可见**；轴长按相机距离保持恒定屏幕尺寸（Round245）；拖动中高亮被选轴。
// 顶点写世界坐标（含平移），用 mvp 投影
// Round354：移动三向标——外观统一为缩放 gizmo 样式（3 轴 + 锥头 + 中心方块），
// 且改用物体本地坐标轴（ax，跟随物体朝向）；中心方块 = 自由拖拽入口（gizmoDragMode==3）。
// 与缩放 gizmo 共用 kUserRed/Green/Blue 配色，外观一致。
static void DrawMoveGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    if (app.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）

    // Round354：本地轴配色（与缩放 gizmo 的 kUserRed/Green/Blue 一致；该常量定义在下方，此处本地定义避免顺序问题）
    static const float kMvRed[3]   = {0.765f, 0.055f, 0.137f};
    static const float kMvGreen[3] = {0.137f, 0.675f, 0.224f};
    static const float kMvBlue[3]  = {0.114f, 0.125f, 0.533f};
    const float cols[3][3] = {{kMvRed[0], kMvRed[1], kMvRed[2]},
                              {kMvGreen[0], kMvGreen[1], kMvGreen[2]},
                              {kMvBlue[0], kMvBlue[1], kMvBlue[2]}};
    const bool dragging = (app.gizmoDragging && app.gizmoDragMode == 1);
    const int hover = (!dragging) ? PickGizmoAxisAt(app, app.mouseX, app.mouseY) : -1;
    // ---- 线框：3 条轴主线（p → 锥底）----
    constexpr int kLineVerts = 3 * 2;
    if (!EnsureHostVtxBuffer(app, app.gizmoVtxBuffer, app.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len * 0.82f,
                                  p[1] + dirs[i][1] * len * 0.82f,
                                  p[2] + dirs[i][2] * len * 0.82f};
            GizmoFillVert(v[vi++], p, col);
            GizmoFillVert(v[vi++], tip, col);
        }
        vkUnmapMemory(app.device, app.gizmoVtxMem);
    }
    // ---- 实体：3 锥头 + 中心方块 ----
    constexpr int kConeVerts = 12 * 6;
    constexpr int kCubeVerts = 12 * 3;
    constexpr int kSolidVerts = kConeVerts * 3 + kCubeVerts;
    if (!EnsureHostVtxBuffer(app, app.gizmoSolidVtxBuffer, app.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            const float base[3] = {p[0] + dirs[i][0] * len * 0.82f,
                                   p[1] + dirs[i][1] * len * 0.82f,
                                   p[2] + dirs[i][2] * len * 0.82f};
            float perpU[3], perpW[3];
            GizmoPerpBasis(dirs[i], perpU, perpW);   // Round354：本地轴垂直基（任意朝向正确截面）
            GizmoFillCone(tip, base, len * 0.05f, perpU, perpW, v, vi, 12, col);
        }
        const float hs = len * 0.085f;   // 中心方块（自由拖拽把手）
        bool centerHit = (app.gizmoDragging && app.gizmoDragMode == 3);
        if (!centerHit) centerHit = HitGizmoRingAt(app, app.mouseX, app.mouseY);
        float cb[4] = {0.96f, 0.96f, 1.0f, 1.0f};
        if (centerHit) for (int c = 0; c < 3; ++c) cb[c] = 1.0f;
        const float c0[3] = {p[0] - hs, p[1] - hs, p[2] - hs};
        const float c1[3] = {p[0] + hs, p[1] + hs, p[2] + hs};
        const float cn[8][3] = {
            {c0[0], c0[1], c0[2]}, {c1[0], c0[1], c0[2]}, {c1[0], c1[1], c0[2]}, {c0[0], c1[1], c0[2]},
            {c0[0], c0[1], c1[2]}, {c1[0], c0[1], c1[2]}, {c1[0], c1[1], c1[2]}, {c0[0], c1[1], c1[2]},
        };
        const int faces[6][4] = {{0,1,2,3},{5,4,7,6},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
        for (int f = 0; f < 6; ++f) {
            const float* a = cn[faces[f][0]];
            const float* b = cn[faces[f][1]];
            const float* cc = cn[faces[f][2]];
            const float* d = cn[faces[f][3]];
            GizmoFillVert(v[vi++], a, cb); GizmoFillVert(v[vi++], b, cb); GizmoFillVert(v[vi++], cc, cb);
            GizmoFillVert(v[vi++], a, cb); GizmoFillVert(v[vi++], cc, cb); GizmoFillVert(v[vi++], d, cb);
        }
        vkUnmapMemory(app.device, app.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineLine3dNoDepth);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kSolidVerts, 1, 0, 0);
}


// ==================== Round298：旋转/缩放 gizmo（Blender 式：3 模式不同样式） ====================

// 旋转 gizmo 的 3 个彩色圆环（绕世界 X/Y/Z 轴）——与移动三向标样式不同
// ==================== Round301：旋转/缩放 gizmo 重做（参考 Blender 外观，颜色用用户 4/5/6.png 三色） ====================
// ============================================================================
// Round302：旋转 / 缩放 gizmo 重做（外观参考 Blender 源码效果，颜色采用 4/5/6.png 三色）
//   旋转（红轴 X）：完整圆环 + 4 象限手柄球 + 中心轴点；拖拽中显示弧线箭头（Blender 样式）
//   缩放（等比）：XYZ 三轴 + 轴末端锥形箭头 + 中心方块把手（Blender 缩放 gizmo 样式）
// ============================================================================
static const float kUserRed[3]   = {0.765f, 0.055f, 0.137f};   // 5.png 旋转红
static const float kUserGreen[3] = {0.137f, 0.675f, 0.224f};   // 6.png 缩放绿
static const float kUserBlue[3]  = {0.114f, 0.125f, 0.533f};   // 6.png 缩放蓝

// Round307：悬停高亮用前置声明（定义在 Draw 函数之后）
static int PickRotateGizmoAt(App& app, float mx, float my);
static int PickScaleGizmoAt(App& app, float mx, float my);

// 实体球体顶点（TRIANGLE_LIST，8 环 x 6 高 = 288 顶点），写入 v[vi...]
static void GizmoFillSphere(const float* c, float rad, VertexSolid* v, int& vi,
                            int sx, int sy, const float* col) {
    for (int iy = 0; iy < sy; ++iy) {
        const float a0 = static_cast<float>(iy) / sy * 3.14159265f;
        const float a1 = static_cast<float>(iy + 1) / sy * 3.14159265f;
        for (int ix = 0; ix < sx; ++ix) {
            const float b0 = static_cast<float>(ix) / sx * 6.2831853f;
            const float b1 = static_cast<float>(ix + 1) / sx * 6.2831853f;
            const float p0[3] = {c[0] + rad * std::sin(a0) * std::cos(b0),
                                 c[1] + rad * std::cos(a0),
                                 c[2] + rad * std::sin(a0) * std::sin(b0)};
            const float p1[3] = {c[0] + rad * std::sin(a0) * std::cos(b1),
                                 c[1] + rad * std::cos(a0),
                                 c[2] + rad * std::sin(a0) * std::sin(b1)};
            const float p2[3] = {c[0] + rad * std::sin(a1) * std::cos(b1),
                                 c[1] + rad * std::cos(a1),
                                 c[2] + rad * std::sin(a1) * std::sin(b1)};
            const float p3[3] = {c[0] + rad * std::sin(a1) * std::cos(b0),
                                 c[1] + rad * std::cos(a1),
                                 c[2] + rad * std::sin(a1) * std::sin(b0)};
            GizmoFillVert(v[vi++], p0, col);
            GizmoFillVert(v[vi++], p1, col);
            GizmoFillVert(v[vi++], p2, col);
            GizmoFillVert(v[vi++], p0, col);
            GizmoFillVert(v[vi++], p2, col);
            GizmoFillVert(v[vi++], p3, col);
        }
    }
}

// 实体圆锥顶点（TRIANGLE_LIST，n 段）：顶点 tip、底面中心 base、底面正交基 u/w
static void GizmoFillCone(const float* tip, const float* base, float rad,
                          const float* u, const float* w,
                          VertexSolid* v, int& vi, int n, const float* col) {
    for (int i = 0; i < n; ++i) {
        const float a0 = static_cast<float>(i) / n * 6.2831853f;
        const float a1 = static_cast<float>(i + 1) / n * 6.2831853f;
        const float p0[3] = {base[0] + rad * (std::cos(a0) * u[0] + std::sin(a0) * w[0]),
                             base[1] + rad * (std::cos(a0) * u[1] + std::sin(a0) * w[1]),
                             base[2] + rad * (std::cos(a0) * u[2] + std::sin(a0) * w[2])};
        const float p1[3] = {base[0] + rad * (std::cos(a1) * u[0] + std::sin(a1) * w[0]),
                             base[1] + rad * (std::cos(a1) * u[1] + std::sin(a1) * w[1]),
                             base[2] + rad * (std::cos(a1) * u[2] + std::sin(a1) * w[2])};
        GizmoFillVert(v[vi++], tip, col);
        GizmoFillVert(v[vi++], p1, col);
        GizmoFillVert(v[vi++], p0, col);
        GizmoFillVert(v[vi++], p0, col);
        GizmoFillVert(v[vi++], p1, col);
        GizmoFillVert(v[vi++], base, col);
    }
}

// Round356：轴对齐小立方体（缩放手柄外观，替代锥头）；c 为中心，hs 为半边长
static void GizmoFillCube(const float* c, float hs, VertexSolid* v, int& vi, const float* col) {
    const float c0[3] = {c[0] - hs, c[1] - hs, c[2] - hs};
    const float c1[3] = {c[0] + hs, c[1] + hs, c[2] + hs};
    const float cn[8][3] = {
        {c0[0], c0[1], c0[2]}, {c1[0], c0[1], c0[2]}, {c1[0], c1[1], c0[2]}, {c0[0], c1[1], c0[2]},
        {c0[0], c0[1], c1[2]}, {c1[0], c0[1], c1[2]}, {c1[0], c1[1], c1[2]}, {c0[0], c1[1], c1[2]},
    };
    const int faces[6][4] = {{0,1,2,3},{5,4,7,6},{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0}};
    for (int f = 0; f < 6; ++f) {
        const float* a = cn[faces[f][0]];
        const float* b = cn[faces[f][1]];
        const float* cc = cn[faces[f][2]];
        const float* d = cn[faces[f][3]];
        GizmoFillVert(v[vi++], a, col); GizmoFillVert(v[vi++], b, col); GizmoFillVert(v[vi++], cc, col);
        GizmoFillVert(v[vi++], a, col); GizmoFillVert(v[vi++], cc, col); GizmoFillVert(v[vi++], d, col);
    }
}

// 旋转 gizmo（Blender 样式）：绕 X 轴（红轴）完整圆环 + 4 象限手柄球 + 中心轴点；
// 拖拽中：弧线箭头显示累计旋转角度（从 0° 到当前角）
// 旋转 gizmo（Blender 标准样式）：3 个彩色圆环（X红 Y绿 Z蓝，Blender 三环）+ 每环 4 象限手柄球 +
// 中心轴点；拖拽中：被拖环高亮 + 弧线箭头显示累计旋转角度
static void DrawRotateGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    if (app.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp);
    const float r = len * 0.78f;
    // Round355：世界轴环基（gizmo 固定不随物体旋转）：0=X 环(YZ) 红 / 1=Y 环(XZ) 绿 / 2=Z 环(XY) 蓝
    float basis[3][2][3] = {
        {{0, 1, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 1, 0}},
    };
    const float cols[3][3] = {{kUserRed[0], kUserRed[1], kUserRed[2]},
                              {kUserGreen[0], kUserGreen[1], kUserGreen[2]},
                              {kUserBlue[0], kUserBlue[1], kUserBlue[2]}};
    const bool dragging = (app.gizmoDragging && app.gizmoDragMode == 4);
    const int dragAxis = dragging ? app.gizmoAxis : -1;
    // Round307：Blender 式悬停高亮——未拖拽时按鼠标位置检测命中环/手柄/中心（-2=中心 trackball）
    const int hover = (!dragging) ? PickRotateGizmoAt(app, app.mouseX, app.mouseY) : -1;
    const int hi = dragging ? dragAxis : hover;
    // ---- 线框：3 环（64 段/环，Round304 已删除拖拽弧线箭头）----
    constexpr int kSegs = 64;
    constexpr int kLineVerts = 3 * kSegs * 2;
    if (!EnsureHostVtxBuffer(app, app.gizmoVtxBuffer, app.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const float* u = basis[i][0];
            const float* w = basis[i][1];
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (hi == i) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            for (int k = 0; k < kSegs; ++k) {
                const float a0 = static_cast<float>(k) / kSegs * 6.2831853f;
                const float a1 = static_cast<float>(k + 1) / kSegs * 6.2831853f;
                float pa[3], pb[3];
                for (int c = 0; c < 3; ++c) {
                    pa[c] = p[c] + r * (std::cos(a0) * u[c] + std::sin(a0) * w[c]);
                    pb[c] = p[c] + r * (std::cos(a1) * u[c] + std::sin(a1) * w[c]);
                }
                GizmoFillVert(v[vi++], pa, col);
                GizmoFillVert(v[vi++], pb, col);
            }
        }
        vkUnmapMemory(app.device, app.gizmoVtxMem);
    }
    // ---- 实体：3 环 × 4 象限手柄球 + 中心轴点 ----
    constexpr int kSphereVerts = 8 * 6 * 6;
    constexpr int kSolidVerts = kSphereVerts * 13;
    if (!EnsureHostVtxBuffer(app, app.gizmoSolidVtxBuffer, app.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        const float hr = r * 0.055f;
        for (int i = 0; i < 3; ++i) {
            const float* u = basis[i][0];
            const float* w = basis[i][1];
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (hi == i) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            for (int q = 0; q < 4; ++q) {
                const float a = q * 3.14159265f * 0.5f;
                float c[3];
                for (int k = 0; k < 3; ++k) c[k] = p[k] + r * (std::cos(a) * u[k] + std::sin(a) * w[k]);
                GizmoFillSphere(c, hr, v, vi, 8, 6, col);
            }
        }
        float centerCol[4] = {0.85f, 0.85f, 0.9f, 1.0f};
        if (hi == -2) for (int c = 0; c < 3; ++c) centerCol[c] = std::min(1.0f, centerCol[c] * 1.5f + 0.3f);
        GizmoFillSphere(p, r * 0.09f, v, vi, 8, 6, centerCol);   // Round307：中心轴点放大（可点击 trackball）
        vkUnmapMemory(app.device, app.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
    // Round305：旋转环 2px 宽线（无深度）；驱动不支持宽线时回退 1px
    const VkPipeline ringPipe = (app.pipelineLine3dNoDepthWide != VK_NULL_HANDLE)
                                    ? app.pipelineLine3dNoDepthWide : app.pipelineLine3dNoDepth;
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ringPipe);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kSolidVerts, 1, 0, 0);
}
static void DrawScaleGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    if (app.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）

    const float cols[3][3] = {{kUserRed[0], kUserRed[1], kUserRed[2]},
                              {kUserGreen[0], kUserGreen[1], kUserGreen[2]},
                              {kUserBlue[0], kUserBlue[1], kUserBlue[2]}};
    const bool dragging = (app.gizmoDragging && app.gizmoDragMode == 5);
    // Round307：Blender 式悬停高亮——未拖拽时按鼠标位置检测命中轴/锥头/中心方块（3=中心）
    const int hover = (!dragging) ? PickScaleGizmoAt(app, app.mouseX, app.mouseY) : -1;
    // ---- 线框：3 条轴主线（p → 轴端立方体）----
    constexpr int kLineVerts = 3 * 2;
    if (!EnsureHostVtxBuffer(app, app.gizmoVtxBuffer, app.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            GizmoFillVert(v[vi++], p, col);
            GizmoFillVert(v[vi++], tip, col);
        }
        vkUnmapMemory(app.device, app.gizmoVtxMem);
    }
    // ---- 实体：3 个小立方体（轴端把手）+ 中心方块（Round356：缩放外观=小立方体，区别于移动锥头）----
    constexpr int kEndCubeVerts = 12 * 3;
    constexpr int kCenterCubeVerts = 12 * 3;
    constexpr int kSolidVerts = kEndCubeVerts * 3 + kCenterCubeVerts;
    if (!EnsureHostVtxBuffer(app, app.gizmoSolidVtxBuffer, app.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.device, app.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        const float hs2 = len * 0.07f;   // 轴端小立方体半边长
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            GizmoFillCube(tip, hs2, v, vi, col);
        }
        float cb[4] = {0.96f, 0.96f, 1.0f, 1.0f};
        if ((dragging && app.gizmoAxis == 3) || (!dragging && hover == 3)) {   // 拖/悬停中心方块提亮
            for (int c = 0; c < 3; ++c) cb[c] = 1.0f;
        }
        GizmoFillCube(p, len * 0.085f, v, vi, cb);   // 中心方块（等比把手）
        vkUnmapMemory(app.device, app.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineLine3dNoDepth);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.commandBuffer, kSolidVerts, 1, 0, 0);
}

// 命中旋转环（环采样 <16px / 手柄球 <18px），命中返回 0，未命中 -1
// 命中旋转环（3 环采样 <16px / 手柄球 <18px），返回命中环轴 0/1/2（X/Y/Z），未命中 -1
// 命中旋转环（3 环采样 <16px / 手柄球 <18px），返回命中环轴 0/1/2（X/Y/Z），未命中 -1
// Round304：三环重叠时优先命中"最正对相机"的环（面向度 = |环法线·视线|），修正优先级错误
// 命中旋转环（3 环采样 <16px / 手柄球 <18px），返回命中环轴 0/1/2（X/Y/Z），未命中 -1
// Round305：优先级 = 鼠标触碰到的最近环（不再按面向度），Blender 交互直觉
static int PickRotateGizmoAt(App& app, float mx, float my) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return -1;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return -1;
    const float len = GizmoAxisLen(app, p, vp);
    const float r = len * 0.78f;
    // Round355：世界轴环基（gizmo 固定不随物体旋转）：与 DrawRotateGizmo 一致
    float basis[3][2][3] = {
        {{0, 1, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 1, 0}},
    };
    float view[16], proj[16], mvp[16];
    app.camera.ViewMatrix(view);
    const float aspect = static_cast<float>(vp.extent.width) / static_cast<float>(vp.extent.height);
    app.camera.ProjectionMatrix(aspect, proj);
    MatMul4(proj, view, mvp);
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    constexpr int kSegs = 64;
    int hitRing = -1;
    float hitDist = 10.0f * 10.0f;   // Round356：旋转模式缩小轴（环）检测范围 16→10px
    for (int i = 0; i < 3; ++i) {   // 三环采样：返回距离最近者
        const float* u = basis[i][0];
        const float* w = basis[i][1];
        float best = 1e9f;
        for (int k = 0; k <= kSegs; ++k) {
            const float a = static_cast<float>(k) / kSegs * 6.2831853f;
            float q[3];
            for (int c = 0; c < 3; ++c) q[c] = p[c] + r * (std::cos(a) * u[c] + std::sin(a) * w[c]);
            float sx, sy;
            if (ProjectToViewport(mvp, gvp, q[0], q[1], q[2], sx, sy)) {
                const float ddx = mx - sx, ddy = my - sy;
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 < best) best = d2;
            }
        }
        if (best < hitDist) {
            hitDist = best;
            hitRing = i;
        }
    }
    if (hitRing >= 0) return hitRing;
    for (int i = 0; i < 3; ++i) {   // 手柄球
        const float* u = basis[i][0];
        const float* w = basis[i][1];
        for (int q = 0; q < 4; ++q) {
            const float a = q * 3.14159265f * 0.5f;
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = p[k] + r * (std::cos(a) * u[k] + std::sin(a) * w[k]);
            float sx, sy;
            if (ProjectToViewport(mvp, gvp, c[0], c[1], c[2], sx, sy)) {
                const float ddx = mx - sx, ddy = my - sy;
                if (ddx * ddx + ddy * ddy < 12.0f * 12.0f) return i;   // Round356：手柄球检测 18→12px
            }
        }
    }
    // Round307：中心轴点命中（放大后可点击）→ 返回 -2（trackball 自由旋转）
    {
        float sx, sy;
        if (ProjectToViewport(mvp, gvp, p[0], p[1], p[2], sx, sy)) {
            const float ddx = mx - sx, ddy = my - sy;
            if (ddx * ddx + ddy * ddy < 24.0f * 24.0f) return -2;
        }
    }
    return -1;
}
static int PickScaleGizmoAt(App& app, float mx, float my) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return -1;
    const SceneObject& o = app.objects[app.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return -1;
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）
    float view[16], proj[16], mvp[16];
    app.camera.ViewMatrix(view);
    const float aspect = static_cast<float>(vp.extent.width) / static_cast<float>(vp.extent.height);
    app.camera.ProjectionMatrix(aspect, proj);
    MatMul4(proj, view, mvp);
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    {
        float sx, sy;
        if (ProjectToViewport(mvp, gvp, p[0], p[1], p[2], sx, sy)) {
            const float ddx = mx - sx, ddy = my - sy;
            if (ddx * ddx + ddy * ddy < 18.0f * 18.0f) return 3;   // 中心方块（等比）
        }
    }
    for (int i = 0; i < 3; ++i) {
        const float tip[3] = {p[0] + dirs[i][0] * len, p[1] + dirs[i][1] * len, p[2] + dirs[i][2] * len};
        float best = 1e9f;
        // Round356：仅采样轴端外段 [0.55,1.0]（小立方体把手所在），与缩放外观对齐
        constexpr int kSteps = 32;
        for (int k = 0; k <= kSteps; ++k) {
            const float t = 0.55f + 0.45f * static_cast<float>(k) / kSteps;
            float q[3];
            for (int c = 0; c < 3; ++c) q[c] = p[c] + (tip[c] - p[c]) * t;
            float sx, sy;
            if (ProjectToViewport(mvp, gvp, q[0], q[1], q[2], sx, sy)) {
                const float ddx = mx - sx, ddy = my - sy;
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 < best) best = d2;
            }
        }
        if (best < 14.0f * 14.0f) return i;
    }
    return -1;
}


// 从 solid 网格提取唯一边 → wireVerts/wireIndices（Tab 线框预览用）
// 构建线框 + 特征边（Round309：参考 Blender Freestyle——crease 棱边法线夹角>30° + border 单面边界边）
static void BuildObjectWireframe(SceneObject& o) {
    o.wireVerts.clear();
    o.wireIndices.clear();
    o.featureVerts.clear();
    const auto& si = o.solidIndices;
    if (si.size() < 3) return;
    // 边 key → 邻接三角形索引（最多记 2 个）
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> adj;
    for (size_t k = 0; k + 2 < si.size(); k += 3) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = si[k + e], b = si[k + (e + 1) % 3];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto& list = adj[key];
            if (list.size() < 2) list.push_back(static_cast<uint32_t>(k / 3));
        }
    }
    constexpr float kCreaseCos = 0.866f;   // cos(30°)——法线夹角 > 30° 视为棱边
    for (const auto& kv : adj) {
        const auto& key = kv.first;
        const auto& tris = kv.second;
        bool feature = false;
        if (tris.size() == 1) {
            feature = true;   // 开放边界边（border，只属于一个面）
        } else {
            const VertexSolid& v0 = o.solidVerts[si[tris[0] * 3]];
            const VertexSolid& v1 = o.solidVerts[si[tris[1] * 3]];
            const float dot = v0.normal[0] * v1.normal[0] +
                              v0.normal[1] * v1.normal[1] +
                              v0.normal[2] * v1.normal[2];
            if (dot < kCreaseCos) feature = true;   // 法线夹角大 → 棱边
        }
        const uint32_t a = key.first, b = key.second;
        o.wireVerts.push_back(o.solidVerts[a]);
        o.wireVerts.push_back(o.solidVerts[b]);
        o.wireIndices.push_back(static_cast<uint32_t>(o.wireVerts.size()) - 2);
        o.wireIndices.push_back(static_cast<uint32_t>(o.wireVerts.size()) - 1);
        if (feature) {
            o.featureVerts.push_back(o.solidVerts[a]);
            o.featureVerts.push_back(o.solidVerts[b]);
        }
    }
}

// ==================== Round359：选中物体外轮廓描边（视相关 silhouette）====================

// 为选中物体构建轮廓边缓存：solidIndices 每条唯一边 → 至多 2 个邻接三角形面法线（本地坐标）。
// 边界边（只属于 1 个面）n1 记 {0,0,0} 标记 → 恒画；视相关判定由 DrawSelectionOutline 逐帧做。
static void BuildSelSilhouette(const SceneObject& o, App& app) {
    app.selSilA.clear(); app.selSilB.clear(); app.selSilN0.clear(); app.selSilN1.clear();
    const auto& si = o.solidIndices;
    const auto& sv = o.solidVerts;
    if (si.size() < 3 || sv.empty()) return;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> adj;
    for (size_t k = 0; k + 2 < si.size(); k += 3) {
        const uint32_t ia = si[k], ib = si[k + 1], ic = si[k + 2];
        if (ia >= sv.size() || ib >= sv.size() || ic >= sv.size()) continue;
        const uint32_t tri = static_cast<uint32_t>(k / 3);
        const std::pair<uint32_t, uint32_t> keys[3] = {
            {std::min(ia, ib), std::max(ia, ib)},
            {std::min(ib, ic), std::max(ib, ic)},
            {std::min(ic, ia), std::max(ic, ia)}};
        for (int e = 0; e < 3; ++e) {
            auto& lst = adj[keys[e]];
            if (lst.size() < 2) lst.push_back(tri);
        }
    }
    const auto triNormal = [&](uint32_t t, float n[3]) {
        n[0] = n[1] = n[2] = 0.0f;
        if (t * 3 + 2 >= si.size()) return;
        const uint32_t ia = si[t * 3], ib = si[t * 3 + 1], ic = si[t * 3 + 2];
        if (ia >= sv.size() || ib >= sv.size() || ic >= sv.size()) return;
        const float* pa = sv[ia].pos; const float* pb = sv[ib].pos; const float* pc = sv[ic].pos;
        const float u0 = pb[0] - pa[0], u1 = pb[1] - pa[1], u2 = pb[2] - pa[2];
        const float w0 = pc[0] - pa[0], w1 = pc[1] - pa[1], w2 = pc[2] - pa[2];
        n[0] = u1 * w2 - u2 * w1;
        n[1] = u2 * w0 - u0 * w2;
        n[2] = u0 * w1 - u1 * w0;
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-9f) { n[0] /= len; n[1] /= len; n[2] /= len; }
    };
    app.selSilA.reserve(adj.size());
    app.selSilB.reserve(adj.size());
    app.selSilN0.reserve(adj.size() * 3);
    app.selSilN1.reserve(adj.size() * 3);
    for (const auto& kv : adj) {
        const auto& tris = kv.second;
        if (tris.empty()) continue;
        app.selSilA.push_back(kv.first.first);
        app.selSilB.push_back(kv.first.second);
        float n0[3], n1[3];
        triNormal(tris[0], n0);
        if (tris.size() >= 2) triNormal(tris[1], n1);
        else { n1[0] = n1[1] = n1[2] = 0.0f; }
        for (int i = 0; i < 3; ++i) { app.selSilN0.push_back(n0[i]); app.selSilN1.push_back(n1[i]); }
    }
}

// 绘制选中物体外轮廓——黄色 2px 线（深度 LEQUAL，线框模式下白线 pass 已跳过选中物体不会覆盖）。
// 画法优先级：A=视相关 silhouette（正面/背面分界边 + 边界边）；轮廓边过多（>20 万）或无可计算数据时
// 回退 B=静态棱边 featureVerts；C=索引线 wireIndices（importer 唯一点格式，修正 Round353 把唯一点当顶点对的 bug）；
// D=顶点对 wireVerts（MC 合并网格）；全部为空 → E=AABB 黄框。
static void DrawSelectionOutline(App& app, const SceneObject& sel, int selIndex, const float mvp[16]) {
    if (app.pipelineLine3d == VK_NULL_HANDLE) return;
    // 缓存失效重建（拓扑不变仅变换时无需重建）
    if (app.selSilIndex != selIndex || app.selSilName != sel.name) {
        BuildSelSilhouette(sel, app);
        app.selSilIndex = selIndex;
        app.selSilName = sel.name;
    }
    enum : int { kSil = 0, kFeature, kIndexed, kPairs, kAabb } mode = kSil;
    const bool big = app.selSilA.size() > 200000;   // 大物体免逐帧判定（轮廓边过多）
    if (app.selSilA.empty() || big) {
        if (!sel.featureVerts.empty())      mode = kFeature;
        else if (!sel.wireVerts.empty() && !sel.wireIndices.empty()) mode = kIndexed;
        else if (!sel.wireVerts.empty())    mode = kPairs;
        else                                mode = kAabb;
    }
    size_t maxVerts = 0;
    switch (mode) {
        case kSil:     maxVerts = app.selSilA.size() * 2; break;
        case kFeature: maxVerts = sel.featureVerts.size(); break;
        case kIndexed: maxVerts = sel.wireIndices.size(); break;
        case kPairs:   maxVerts = sel.wireVerts.size(); break;
        case kAabb:    maxVerts = 24; break;
    }
    if (maxVerts == 0) return;

    // 确保顶点缓冲容量（复用 selVtxBuffer/selVtxMem/selVtxCapacity）
    const VkDeviceSize need = static_cast<VkDeviceSize>(maxVerts) * 40;
    if (app.selVtxBuffer != VK_NULL_HANDLE && app.selVtxCapacity < need) {
        vkDestroyBuffer(app.device, app.selVtxBuffer, nullptr);
        vkFreeMemory(app.device, app.selVtxMem, nullptr);
        app.selVtxBuffer = VK_NULL_HANDLE;
        app.selVtxMem = VK_NULL_HANDLE;
        app.selVtxCapacity = 0;
    }
    if (app.selVtxBuffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = need;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(app.device, &bi, nullptr, &app.selVtxBuffer);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, app.selVtxBuffer, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        vkAllocateMemory(app.device, &ai, nullptr, &app.selVtxMem);
        vkBindBufferMemory(app.device, app.selVtxBuffer, app.selVtxMem, 0);
        app.selVtxCapacity = need;
    }

    float model[16] = {};
    BuildModelMatrix(sel, model);
    void* p = nullptr;
    if (vkMapMemory(app.device, app.selVtxMem, 0, need, 0, &p) != VK_SUCCESS) return;
    VertexSolid* v = static_cast<VertexSolid*>(p);
    size_t vi = 0;
    if (mode == kSil) {
        // 视相关轮廓：边中点→相机方向，两邻接面法线一正一负（或边界边）→ 轮廓
        const float r00 = model[0], r01 = model[1], r02 = model[2], tx = model[12];
        const float r10 = model[4], r11 = model[5], r12 = model[6], ty = model[13];
        const float r20 = model[8], r21 = model[9], r22 = model[10], tz = model[14];
        const auto& sv = sel.solidVerts;
        const float cxp = app.camera.position.x, cyp = app.camera.position.y, czp = app.camera.position.z;
        for (size_t i = 0; i < app.selSilA.size(); ++i) {
            const uint32_t ia = app.selSilA[i], ib = app.selSilB[i];
            if (ia >= sv.size() || ib >= sv.size()) continue;
            const VertexSolid& va = sv[ia];
            const VertexSolid& vb = sv[ib];
            const float wax = r00 * va.pos[0] + r01 * va.pos[1] + r02 * va.pos[2] + tx;
            const float way = r10 * va.pos[0] + r11 * va.pos[1] + r12 * va.pos[2] + ty;
            const float waz = r20 * va.pos[0] + r21 * va.pos[1] + r22 * va.pos[2] + tz;
            const float wbx = r00 * vb.pos[0] + r01 * vb.pos[1] + r02 * vb.pos[2] + tx;
            const float wby = r10 * vb.pos[0] + r11 * vb.pos[1] + r12 * vb.pos[2] + ty;
            const float wbz = r20 * vb.pos[0] + r21 * vb.pos[1] + r22 * vb.pos[2] + tz;
            const float mdx = (wax + wbx) * 0.5f - cxp;
            const float mdy = (way + wby) * 0.5f - cyp;
            const float mdz = (waz + wbz) * 0.5f - czp;
            const float inv = 1.0f / (std::sqrt(mdx * mdx + mdy * mdy + mdz * mdz) + 1e-9f);
            const float cdx = mdx * inv, cdy = mdy * inv, cdz = mdz * inv;
            const float n0x = app.selSilN0[i * 3], n0y = app.selSilN0[i * 3 + 1], n0z = app.selSilN0[i * 3 + 2];
            const float n1x = app.selSilN1[i * 3], n1y = app.selSilN1[i * 3 + 1], n1z = app.selSilN1[i * 3 + 2];
            bool draw;
            if (n1x == 0.0f && n1y == 0.0f && n1z == 0.0f) {
                draw = true;   // 边界边（开放网格外沿）恒画
            } else {
                const float d0 = (r00 * n0x + r01 * n0y + r02 * n0z) * cdx +
                                 (r10 * n0x + r11 * n0y + r12 * n0z) * cdy +
                                 (r20 * n0x + r21 * n0y + r22 * n0z) * cdz;
                const float d1 = (r00 * n1x + r01 * n1y + r02 * n1z) * cdx +
                                 (r10 * n1x + r11 * n1y + r12 * n1z) * cdy +
                                 (r20 * n1x + r21 * n1y + r22 * n1z) * cdz;
                draw = (d0 * d1 < 0.0f);   // 一正面一背面 → 轮廓边
            }
            if (draw) { v[vi++] = va; v[vi++] = vb; }
        }
    } else if (mode == kFeature) {
        for (const auto& a : sel.featureVerts) v[vi++] = a;
    } else if (mode == kIndexed) {
        for (size_t i = 0; i + 1 < sel.wireIndices.size(); i += 2) {
            const uint32_t a = sel.wireIndices[i], b = sel.wireIndices[i + 1];
            if (a < sel.wireVerts.size() && b < sel.wireVerts.size()) { v[vi++] = sel.wireVerts[a]; v[vi++] = sel.wireVerts[b]; }
        }
    } else if (mode == kPairs) {
        for (const auto& a : sel.wireVerts) v[vi++] = a;
    } else {   // kAabb：黄色 AABB 12 边
        const float minx = sel.boundsMin[0], miny = sel.boundsMin[1], minz = sel.boundsMin[2];
        const float maxx = sel.boundsMax[0], maxy = sel.boundsMax[1], maxz = sel.boundsMax[2];
        const float c[8][3] = {
            {minx, miny, minz}, {maxx, miny, minz}, {minx, maxy, minz}, {maxx, maxy, minz},
            {minx, miny, maxz}, {maxx, miny, maxz}, {minx, maxy, maxz}, {maxx, maxy, maxz}};
        const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{3,7},{2,6}};
        for (int e = 0; e < 12; ++e) for (int k = 0; k < 2; ++k) {
            VertexSolid& vv = v[vi++];
            const int ci = edges[e][k];
            vv.pos[0] = c[ci][0]; vv.pos[1] = c[ci][1]; vv.pos[2] = c[ci][2];
        }
    }
    // 统一黄色 + 法线占位（line3d shader 只读 pos+color）
    for (size_t i = 0; i < vi; ++i) {
        v[i].normal[0] = 0.0f; v[i].normal[1] = 1.0f; v[i].normal[2] = 0.0f;
        v[i].color[0] = 1.0f; v[i].color[1] = 0.84f; v[i].color[2] = 0.1f; v[i].color[3] = 1.0f;   // 黄
    }
    vkUnmapMemory(app.device, app.selVtxMem);

    float mvpm[16];
    MatMul4(mvp, model, mvpm);
    VkPipeline pipe = (app.pipelineLine3dWide != VK_NULL_HANDLE) ? app.pipelineLine3dWide : app.pipelineLine3d;
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.selVtxBuffer, &off);
    vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvpm);
    vkCmdDraw(app.commandBuffer, static_cast<uint32_t>(vi), 1, 0, 0);
}

// ==================== 撤销/重做（Round250：Ctrl+Z 撤销 / Ctrl+B 重做）====================

void PushUndo(App& app, const App::UndoEntry& e) {
    app.undoStack.push_back(e);
    if (static_cast<int>(app.undoStack.size()) > App::kUndoCapacity)
        app.undoStack.erase(app.undoStack.begin());   // 超出容量丢最旧
    app.redoStack.clear();                            // 新操作清空重做栈
}

// 撤销/重做后：重建顶点缓冲 + 修正选中索引 + 复位线框/三向标状态
static void RebuildAfterUndoRedo(App& app) {
    vkDeviceWaitIdle(app.device);
    CreateVertexBuffer3D(app);
    if (app.selectedObject >= static_cast<int>(app.objects.size()))
        app.selectedObject = static_cast<int>(app.objects.size()) - 1;
    app.wireframeSel = false;
    app.gizmoDragging = false;
    app.gizmoAxis = -1;
}

static void Undo(App& app) {
    if (app.undoStack.empty()) return;
    App::UndoEntry e = app.undoStack.back();
    app.undoStack.pop_back();
    switch (e.op) {
    case App::UndoOp::Add:    // 撤销添加（导入/复制）：移除该物体
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size()))
            app.objects.erase(app.objects.begin() + e.index);
        break;
    case App::UndoOp::Remove: // 撤销删除：恢复该物体（副本插入，快照保持有效）
        if (e.index >= 0 && e.index <= static_cast<int>(app.objects.size()))
            app.objects.insert(app.objects.begin() + e.index, e.obj);
        break;
    case App::UndoOp::Move:   // 撤销移动：还原操作前位置
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.tx = e.oldTx; o.ty = e.oldTy; o.tz = e.oldTz;
        }
        break;
    case App::UndoOp::Rotate: // 撤销旋转：还原操作前欧拉角
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.rx = e.oldRx; o.ry = e.oldRy; o.rz = e.oldRz;
        }
        break;
    case App::UndoOp::Scale:  // 撤销缩放：还原操作前缩放
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.sx = e.oldSx; o.sy = e.oldSy; o.sz = e.oldSz;
        }
        break;
    }
    app.redoStack.push_back(e);
    if (static_cast<int>(app.redoStack.size()) > App::kRedoCapacity)
        app.redoStack.erase(app.redoStack.begin());
    RebuildAfterUndoRedo(app);
}

static void Redo(App& app) {
    if (app.redoStack.empty()) return;
    App::UndoEntry e = app.redoStack.back();
    app.redoStack.pop_back();
    switch (e.op) {
    case App::UndoOp::Add:    // 重做添加：重新插入（副本，快照保持有效）
        if (e.index >= 0 && e.index <= static_cast<int>(app.objects.size()))
            app.objects.insert(app.objects.begin() + e.index, e.obj);
        break;
    case App::UndoOp::Remove: // 重做删除：再次移除
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size()))
            app.objects.erase(app.objects.begin() + e.index);
        break;
    case App::UndoOp::Move:   // 重做移动：还原到操作后位置
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.tx = e.newTx; o.ty = e.newTy; o.tz = e.newTz;
        }
        break;
    case App::UndoOp::Rotate: // 重做旋转：还原到操作后欧拉角
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.rx = e.newRx; o.ry = e.newRy; o.rz = e.newRz;
        }
        break;
    case App::UndoOp::Scale:  // 重做缩放：还原到操作后缩放
        if (e.index >= 0 && e.index < static_cast<int>(app.objects.size())) {
            SceneObject& o = app.objects[e.index];
            o.sx = e.newSx; o.sy = e.newSy; o.sz = e.newSz;
        }
        break;
    }
    app.undoStack.push_back(e);
    if (static_cast<int>(app.undoStack.size()) > App::kUndoCapacity)
        app.undoStack.erase(app.undoStack.begin());
    RebuildAfterUndoRedo(app);
}

static void DeleteSelectedObject(App& app) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    App::UndoEntry e;
    e.op = App::UndoOp::Remove;
    e.index = app.selectedObject;
    e.obj = app.objects[app.selectedObject];   // 快照（副本）
    PushUndo(app, e);
    vkDeviceWaitIdle(app.device);
    app.objects.erase(app.objects.begin() + app.selectedObject);
    app.selectedObject = -1;
    app.wireframeSel = false;
    CreateVertexBuffer3D(app);
}

static void DuplicateSelectedObject(App& app) {
    if (app.selectedObject < 0 || app.selectedObject >= static_cast<int>(app.objects.size())) return;
    vkDeviceWaitIdle(app.device);
    SceneObject copy = app.objects[app.selectedObject];
    copy.tx += 1.5f;   // 偏移避免与原件重叠
    copy.name = copy.name + L" (复制)";
    app.objects.push_back(std::move(copy));
    app.selectedObject = static_cast<int>(app.objects.size()) - 1;
    App::UndoEntry e;
    e.op = App::UndoOp::Add;
    e.index = app.selectedObject;
    e.obj = app.objects[app.selectedObject];   // 快照（副本）
    PushUndo(app, e);
    CreateVertexBuffer3D(app);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_GETMINMAXINFO:  // 最小窗口尺寸（防止拖到过小导致交换链/布局异常）
        if (MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam)) {
            mmi->ptMinTrackSize.x = 320;
            mmi->ptMinTrackSize.y = 240;
        }
        return 0;
    case WM_SIZE:
        if (App* app = GetApp(hwnd)) {
            const uint32_t w = static_cast<uint32_t>(LOWORD(lParam));
            const uint32_t h = static_cast<uint32_t>(HIWORD(lParam));
            if (w != app->swapchainExtent.width || h != app->swapchainExtent.height) {
                app->resizePending = true;
            }
            if (wParam != SIZE_MINIMIZED && w > 0 && h > 0 && app->swapchain != VK_NULL_HANDLE) {
                DrawFrame(*app);
            }
        }
        return 0;
    case WM_SETCURSOR:   // Round362：悬停可拖拽分隔线/拖拽中 -> Windows 缩放光标（随方向：水平线=上下箭头 IDC_SIZENS / 垂直线=左右箭头 IDC_SIZEWE）
        if (App* app = GetApp(hwnd)) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const int div = (app->resizeDrag >= 0)
                                ? app->resizeDrag
                                : HitResizeDivider(*app, static_cast<float>(pt.x), static_cast<float>(pt.y));
            if (div >= 0) {
                // 32645=IDC_SIZENS（上下箭头） 32644=IDC_SIZEWE（左右箭头）；工程无 UNICODE 需用 W 版宏
                const int cid = (div == 0 || div == 3) ? 32645 : 32644;
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cid)));
                return TRUE;
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);   // 非分隔线：默认箭头（本函数所有 case 均 return，不用 break）
    case WM_KEYDOWN:
        if (App* app = GetApp(hwnd)) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_DELETE) {
                DeleteSelectedObject(*app);                       // Delete：删除选中
            } else if (wParam == 'D' && ctrl) {
                DuplicateSelectedObject(*app);                    // Ctrl+D：复制选中
            } else if (wParam == 'Z' && ctrl) {
                Undo(*app);                                       // Ctrl+Z：撤销（Round250）
            } else if (wParam == 'B' && ctrl) {
                Redo(*app);                                       // Ctrl+B：重做（Round250）
            } else if (wParam == VK_TAB) {
                if (app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    app->wireframeSel = !app->wireframeSel;       // Tab：线框预览
                    if (app->wireframeSel && app->objects[app->selectedObject].wireVerts.empty()) {
                        BuildObjectWireframe(app->objects[app->selectedObject]);
                        vkDeviceWaitIdle(app->device);
                        CreateVertexBuffer3D(*app);
                    }
                }
            } else if (wParam == 'E' && !ctrl) {
                app->gizmoMode = 0;                               // Round313：E=移动物体
            } else if (wParam == 'R' && !ctrl) {
                app->gizmoMode = 1;                               // Round313：R=旋转物体
            } else if (wParam == 'T' && !ctrl) {
                app->gizmoMode = 2;                               // Round313：T=缩放物体
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (App* app = GetApp(hwnd)) {
            // Round305：取消滚轮缩放物体模型（用户要求）——滚轮仅缩放相机视角
            const int wheel = GET_WHEEL_DELTA_WPARAM(wParam);
            // Round320：缩放 → 显示左下角距离文字
            app->navLastActionMs = GetTickCount64();
            app->navLastActionType = 2;
            app->camera.Zoom(static_cast<float>(wheel) / WHEEL_DELTA);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
            const float mx = MouseX(lParam);
            const float my = MouseY(lParam);
            if (app->menuOpen) {
                for (int i = 0; i < 2; ++i) {
                    if (app->menuItems[i].machine.OnMouseDown(PointInButton(app->menuItems[i], mx, my))) {
                        app->pressedMenuItem = i;
                        return 0;
                    }
                }
                constexpr float kMenuPad = 10.0f;
                constexpr float kMenuItemH = 30.0f;
                const float menuH = kMenuPad * 2.0f + kMenuItemH;
                if (mx >= static_cast<float>(app->menuRect.offset.x) &&
                    mx <  static_cast<float>(app->menuRect.offset.x + static_cast<int32_t>(app->menuRect.extent.width)) &&
                    my >= static_cast<float>(app->menuRect.offset.y) &&
                    my <  static_cast<float>(app->menuRect.offset.y) + menuH) {
                    return 0;
                }
            }
            // Round358：分隔线拖拽开始（优先于按钮/物体拾取；命中 4 条可缩放边缘）
            const int divider = HitResizeDivider(*app, mx, my);
            if (divider >= 0) {
                app->resizeDrag = divider;
                app->resizeStartMouse = (divider == 1 || divider == 2) ? mx : my;
                app->resizeStartVal = (divider == 0) ? app->panelTopH :
                                      (divider == 1) ? app->panelLeftW :
                                      (divider == 2) ? app->panelRightW : app->panelBottomH;
                app->mouseDragged = true;
                SetCapture(hwnd);
                return 0;
            }
            int hit = -1;
            for (size_t i = 0; i < app->buttons.size(); ++i) {
                if (app->buttons[i].machine.OnMouseDown(PointInButton(app->buttons[i], mx, my))) {
                    hit = static_cast<int>(i);
                    break;
                }
            }
            if (hit >= 0) {
                app->pressedButton = hit;
                if (app->buttons[hit].icon != 1) app->menuOpen = false;
            } else {
                app->menuOpen = false;
                // 右侧物体列表点击选中（Round252：Blender 风格多物体编辑栏；Round358：跟随可调右栏布局）
                const Layout layR = ComputeLayout(*app);
                if (layR.right.extent.width >= 40 && layR.right.extent.height >= 100) {
                    const int px = layR.right.offset.x + kObjPanelPad;
                    const int py = layR.right.offset.y + kObjPanelPad;
                    const int pw = static_cast<int>(layR.right.extent.width) - 2 * kObjPanelPad;
                    const int ph = static_cast<int>(layR.right.extent.height) - 2 * kObjPanelPad;
                    if (mx >= px && mx < px + pw && my >= py && my < py + ph) {
                        const int row = static_cast<int>((my - py) / kObjPanelRowH);
                        if (row >= 0 && row < static_cast<int>(app->objects.size())) {
                            // Round298：已选中同一行 → 取消选择；否则选中该行（Round329：单选清空框选多选）
                            app->multiSel.clear();
                            if (app->selectedObject == row) {
                                app->selectedObject = -1;
                                app->wireframeSel = false;
                            } else {
                                app->selectedObject = row;
                                app->wireframeSel = false;
                            }
                        }
                        app->mouseDragged = true;   // 屏蔽松开时的物体拾取
                        return 0;
                    }
                }
                // 左上角 3 个圆形按钮（Round248：标准按钮动画）
                for (int i = 0; i < 3; ++i) {
                    if (app->ballButtons[i].machine.OnMouseDown(PointInButton(app->ballButtons[i], mx, my))) {
                        app->pressedBall = i;
                        app->mouseDragged = true;   // 屏蔽松开时的物体拾取
                        return 0;
                    }
                }
                // Round302：旋转模式（gizmoMode==1）——命中三色环/手柄球才拖拽（Blender 式：未命中不拦截，可正常 orbit 视角）
                if (app->gizmoMode == 1 && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    const int ringAxis = PickRotateGizmoAt(*app, mx, my);
                    if (ringAxis != -1) {   // 0/1/2=环轴，-2=中心轴点（trackball 自由旋转）
                        SceneObject& so = app->objects[app->selectedObject];
                        app->gizmoAxis = ringAxis;           // 0/1/2 = X/Y/Z 环（红/绿/蓝）
                        app->gizmoDragMode = 4;              // 4=旋转拖拽
                        app->gizmoDragging = true;
                        app->gizmoStartRx = so.rx; app->gizmoStartRy = so.ry; app->gizmoStartRz = so.rz;
                        app->pressX = mx; app->pressY = my;
                        // Round304：记录枢轴的屏幕投影（旋转角度基准，沿环切向拖拽）
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
                                                   app->gizmoScreenPivotX, app->gizmoScreenPivotY)) {
                                app->gizmoScreenPivotX = mx;
                                app->gizmoScreenPivotY = my;
                            }
                        }
                        app->mouseDragged = true;
                        SetCapture(hwnd);
                        return 0;
                    }
                }
                // Round298：缩放模式（gizmoMode==2）——命中缩放轴/中心 → 拖拽等比缩放
                if (app->gizmoMode == 2 && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    const int axis = PickScaleGizmoAt(*app, mx, my);
                    if (axis >= 0) {
                        SceneObject& so = app->objects[app->selectedObject];
                        app->gizmoAxis = axis;           // 0/1/2=轴 3=中心（等比）
                        app->gizmoDragMode = 5;          // 5=缩放拖拽
                        app->gizmoDragging = true;
                        app->gizmoStartSx = so.sx; app->gizmoStartSy = so.sy; app->gizmoStartSz = so.sz;
                        app->pressX = mx; app->pressY = my;
                        app->mouseDragged = true;
                        SetCapture(hwnd);
                        return 0;
                    }
                }
                // 移动三向标抓取（选中物体时优先于 orbit；Round298：仅移动模式 gizmoMode==0 命中）
                if (app->gizmoMode == 0 && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    // 中心环（Round260：Blender 拖中心球 → 视口平面任意方向自由移动）。
                    // Round263：白边（选中框边线）拖拽已按用户要求**删除**，不再作为移动把手。
                    if (HitGizmoRingAt(*app, mx, my)) {
                        StartFreeDrag(*app, hwnd, mx, my);
                        return 0;
                    }
                    // 命中某根轴 → 沿轴拖拽
                    const int axis = PickGizmoAxisAt(*app, mx, my);
                    if (axis >= 0) {
                        SceneObject& so = app->objects[app->selectedObject];
                        app->gizmoAxis = axis;
                        app->gizmoDragMode = 1;
                        app->gizmoDragging = true;
                        app->gizmoStartTx = so.tx; app->gizmoStartTy = so.ty; app->gizmoStartTz = so.tz;
                        GizmoPivot(so, app->gizmoPivot);
                        const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）
                        float o[3], d[3];
                        float t = 0.0f;
                        if (BuildViewRay(*app, mx, my, o, d)) ClosestAxisParam(app->gizmoPivot, dirs[axis], o, d, t);
                        app->gizmoStartT = t;
                        app->pressX = mx;          // 记录按下位置（区分点击/拖拽）
                        app->pressY = my;
                        app->mouseDragged = true;  // 三向标按下即视为拖拽：松开时不重新拾取（防误取消选中）
                        SetCapture(hwnd);
                        return 0;
                    }
                }
                // Round332：左键 = 移动视角（orbit，原中键功能）——空白处按下记录，**移动>4px 才进入 orbit**
                // （gizmo/UI 命中已在上方 return，此处仅空白处）
                app->camera.orbiting = false;
                app->camera.lastX = mx;
                app->camera.lastY = my;
                app->lbuttonDown = true;   // 左键按住标志（MOUSEMOVE 检测移动>4px → orbit）
                app->pressX = mx;          // 记录按下位置（区分点击/拖拽）
                app->pressY = my;
                app->mouseDragged = false;
                SetCapture(hwnd);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (App* app = GetApp(hwnd)) {
            // Round358：分隔线拖拽结束（尺寸已实时更新）
            if (app->resizeDrag >= 0) {
                app->resizeDrag = -1;
                ReleaseCapture();
                // Round364：拖拽结束 → 面板尺寸写入 awa_settings.txt（顶栏固定不保存；下次启动恢复左/右/底）
                SaveSettingInt("panel_left_w", static_cast<int>(app->panelLeftW));
                SaveSettingInt("panel_right_w", static_cast<int>(app->panelRightW));
                SaveSettingInt("panel_bottom_h", static_cast<int>(app->panelBottomH));
            }
            // 三向标/中心环拖拽结束（松开即提交；位置有变化才记录撤销项）
            if (app->gizmoDragging) {
                app->gizmoDragging = false;
                // Round296：旋转拖拽结束（gizmoDragMode==4）——有变化才记录撤销
                if (app->gizmoDragMode == 4 && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    SceneObject& so = app->objects[app->selectedObject];
                    if (so.rx != app->gizmoStartRx || so.ry != app->gizmoStartRy || so.rz != app->gizmoStartRz) {
                        App::UndoEntry e;
                        e.op = App::UndoOp::Rotate;
                        e.index = app->selectedObject;
                        e.oldRx = app->gizmoStartRx; e.oldRy = app->gizmoStartRy; e.oldRz = app->gizmoStartRz;
                        e.newRx = so.rx;              e.newRy = so.ry;              e.newRz = so.rz;
                        PushUndo(*app, e);
                    }
                }
                // Round298：缩放拖拽结束（gizmoDragMode==5）——有变化才记录撤销
                if (app->gizmoDragMode == 5 && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    SceneObject& so = app->objects[app->selectedObject];
                    if (so.sx != app->gizmoStartSx || so.sy != app->gizmoStartSy || so.sz != app->gizmoStartSz) {
                        App::UndoEntry e;
                        e.op = App::UndoOp::Scale;
                        e.index = app->selectedObject;
                        e.oldSx = app->gizmoStartSx; e.oldSy = app->gizmoStartSy; e.oldSz = app->gizmoStartSz;
                        e.newSx = so.sx;              e.newSy = so.sy;              e.newSz = so.sz;
                        PushUndo(*app, e);
                    }
                }
                if ((app->gizmoAxis >= 0 || app->gizmoDragMode == 3) && app->selectedObject >= 0 &&
                    app->selectedObject < static_cast<int>(app->objects.size())) {
                    SceneObject& so = app->objects[app->selectedObject];
                    if (so.tx != app->gizmoStartTx || so.ty != app->gizmoStartTy || so.tz != app->gizmoStartTz) {
                        App::UndoEntry e;
                        e.op = App::UndoOp::Move;
                        e.index = app->selectedObject;
                        e.oldTx = app->gizmoStartTx; e.oldTy = app->gizmoStartTy; e.oldTz = app->gizmoStartTz;
                        e.newTx = so.tx;              e.newTy = so.ty;              e.newTz = so.tz;
                        PushUndo(*app, e);
                    }
                }
                app->gizmoAxis = -1;
                app->gizmoDragMode = 0;
                app->gizmoLastHitValid = false;
            }
            // 左上角圆形按钮释放（Round248；Round266：球1=线框模式 球2=实体模式 球3暂定）
            if (app->pressedBall >= 0) {
                const int idx = app->pressedBall;
                app->pressedBall = -1;
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->ballButtons[idx], ux, uy);
                app->ballButtons[idx].machine.OnMouseUp(inside);
                if (inside) {
                    if (idx == 0) app->renderMode = 1;   // 线框模式
                    else if (idx == 1) app->renderMode = 0;  // 实体模式
                    // idx==2：暂定（无功能）
                }
            }
            if (app->pressedMenuItem >= 0) {
                const int idx = app->pressedMenuItem;
                app->pressedMenuItem = -1;
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->menuItems[idx], ux, uy);
                app->menuItems[idx].machine.OnMouseUp(inside);
                if (inside && app->menuItems[idx].onClick) app->menuItems[idx].onClick(*app);
            }
            if (app->pressedButton >= 0) {
                const int idx = app->pressedButton;
                app->pressedButton = -1;
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const bool inside = PointInButton(app->buttons[idx], ux, uy);
                app->buttons[idx].machine.OnMouseUp(inside);
                if (inside) {
                    // Round296：顶栏后 3 个按钮=变换模式（移动/旋转/缩放）——按按钮索引设置 gizmoMode
                    if (app->buttons.size() >= 3 &&
                        idx >= static_cast<int>(app->buttons.size()) - 3) {
                        app->gizmoMode = idx - (static_cast<int>(app->buttons.size()) - 3);
                    }
                    if (app->buttons[idx].onClick) app->buttons[idx].onClick(*app);
                }
            }
            // Round332：左键 = 移动视角（orbit，原中键功能）——松开结束 orbit 视角拖动
            app->camera.orbiting = false;
            app->lbuttonDown = false;
            // 左键【点击】（非拖拽）且未点按钮 → 3D 视口拾取物体
            if (!app->mouseDragged && app->pressedButton < 0 && app->pressedMenuItem < 0) {
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const Layout lay = ComputeLayout(*app);
                const VkRect2D& vp = lay.viewport;
                if (ux >= static_cast<float>(vp.offset.x) &&
                    ux <  static_cast<float>(vp.offset.x + vp.extent.width) &&
                    uy >= static_cast<float>(vp.offset.y) &&
                    uy <  static_cast<float>(vp.offset.y + vp.extent.height)) {
                    const int picked = PickObjectAt(*app, ux, uy);
                    // Round329：单击单选 → 清空框选多选
                    app->multiSel.clear();
                    // 再次点击已选中物体 → 取消选中（Round240）
                    if (picked == app->selectedObject && picked >= 0) {
                        app->selectedObject = -1;
                        app->wireframeSel = false;
                    } else {
                        app->selectedObject = picked;
                        if (app->selectedObject < 0) app->wireframeSel = false;
                    }
                }
            }
        }
        ReleaseCapture();
        return 0;
    case WM_MBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
            // Round332：中键 = 框选目标（原左键功能）——空白处按下开始框选
            // （中键不操作 gizmo，无需 gizmo/UI 命中检测；滚轮缩放由 WM_MOUSEWHEEL 单独处理）
            app->menuOpen = false;
            app->marqueeSelecting = true;
            app->marqueeX0 = MouseX(lParam); app->marqueeY0 = MouseY(lParam);
            app->marqueeX1 = app->marqueeX0; app->marqueeY1 = app->marqueeY0;
            app->pressX = app->marqueeX0;   // 记录按下位置（区分点击/框选拖拽）
            app->pressY = app->marqueeY0;
            app->mouseDragged = false;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        if (App* app = GetApp(hwnd)) {
            // Round332：中键 = 框选（原左键功能），松开完成框选
            if (app->marqueeSelecting) {
                app->marqueeSelecting = false;
                const float ux = MouseX(lParam);
                const float uy = MouseY(lParam);
                const float x0 = std::min(app->marqueeX0, ux);
                const float y0 = std::min(app->marqueeY0, uy);
                const float x1 = std::max(app->marqueeX0, ux);
                const float y1 = std::max(app->marqueeY0, uy);
                if (x1 - x0 >= 4.0f && y1 - y0 >= 4.0f) {
                    // 框选：物体中心（世界平移位置）投影到屏幕，在框内 → 选中（主操作对象=最后一个）
                    app->multiSel.clear();
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
                    for (int i = 0; i < static_cast<int>(app->objects.size()); ++i) {
                        const SceneObject& o = app->objects[i];
                        float sx, sy;
                        if (ProjectToViewport(mvp, gvp, o.tx, o.ty, o.tz, sx, sy)) {
                            if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {
                                app->multiSel.push_back(i);
                                lastSel = i;
                            }
                        }
                    }
                    app->wireframeSel = false;
                    if (lastSel >= 0) {
                        app->selectedObject = lastSel;   // 主操作对象 = 最后一个框内物体
                    } else {
                        app->selectedObject = -1;        // 框到空白 → 取消选择
                        app->multiSel.clear();
                    }
                    app->mouseDragged = true;   // 框选完成：跳过单击拾取
                }
                // 框太小（中键单击）→ 不框选、不拾取（中键语义=框选工具，单击无效）
            }
        }
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        if (App* app = GetApp(hwnd)) {
            app->menuOpen = false;
            app->camera.panning = true;
            app->camera.lastX = MouseX(lParam);
            app->camera.lastY = MouseY(lParam);
            SetCapture(hwnd);
        }
        return 0;
    case WM_RBUTTONUP:
        if (App* app = GetApp(hwnd)) app->camera.panning = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (App* app = GetApp(hwnd)) {
            const float x = MouseX(lParam);
            const float y = MouseY(lParam);
            app->mouseX = x;              // 悬停高亮（中心环）用
            app->mouseY = y;
            // Round358：分隔线拖拽——实时调整面板尺寸（min=默认值，max 保证视口 >=60px）
            if (app->resizeDrag >= 0) {
                const float m = (app->resizeDrag == 1 || app->resizeDrag == 2) ? x : y;
                const int64_t delta = static_cast<int64_t>(m - app->resizeStartMouse);
                // Round362：统一拖拽方向（Windows 缩放直觉——边缘朝哪拖面板就往哪扩展）：
                // 顶栏(0)下缘向下拖变高、左栏(1)右缘向右拖变宽（+）；右栏(2)左缘向左拖变宽、
                // 底栏(3)上缘向上拖变高 → 与左/顶相反，取反
                const int64_t deltaSigned = (app->resizeDrag == 2 || app->resizeDrag == 3) ? -delta : delta;
                uint32_t* val = (app->resizeDrag == 0) ? &app->panelTopH :
                                (app->resizeDrag == 1) ? &app->panelLeftW :
                                (app->resizeDrag == 2) ? &app->panelRightW : &app->panelBottomH;
                const uint32_t minVal = (app->resizeDrag == 0) ? kTopBarHeight :
                                        (app->resizeDrag == 1 || app->resizeDrag == 2) ? kSideBarWidth :
                                                                                         kBottomBarHeight;
                const uint32_t W = app->swapchainExtent.width;
                const uint32_t H = app->swapchainExtent.height;
                constexpr uint32_t kViewportMin = 60;   // 视口最小保留
                uint32_t maxVal = minVal;
                if (app->resizeDrag == 0)      maxVal = (H > app->panelBottomH + kViewportMin) ? H - app->panelBottomH - kViewportMin : minVal;
                else if (app->resizeDrag == 3) maxVal = (H > app->panelTopH + kViewportMin)    ? H - app->panelTopH    - kViewportMin : minVal;
                else if (app->resizeDrag == 1) maxVal = (W > app->panelRightW + kViewportMin)  ? W - app->panelRightW  - kViewportMin : minVal;
                else                           maxVal = (W > app->panelLeftW + kViewportMin)   ? W - app->panelLeftW   - kViewportMin : minVal;
                int64_t nv = static_cast<int64_t>(app->resizeStartVal) + deltaSigned;
                nv = std::max<int64_t>(minVal, std::min<int64_t>(nv, maxVal));
                *val = static_cast<uint32_t>(nv);
                return 0;   // 拖拽分隔线期间屏蔽其它交互
            }
            const float dx = x - app->camera.lastX;
            const float dy = y - app->camera.lastY;
            // Round304：旋转拖拽（gizmoDragMode==4）——沿环切向：鼠标绕枢轴屏幕投影的角度差（顺时针拖=正转，径向拖不转）
            if (app->gizmoDragging && app->gizmoDragMode == 4 &&
                app->selectedObject >= 0 &&
                app->selectedObject < static_cast<int>(app->objects.size())) {
                SceneObject& so = app->objects[app->selectedObject];
                if (app->gizmoAxis == -2) {   // Round307/355：中心 trackball——水平拖绕世界Y、垂直拖绕世界X（世界空间，朝向不影响方向）
                    float R0[16], Ry[16], Rx[16], R1[16], Rnew[16];
                    BuildRotFromEuler(app->gizmoStartRx, app->gizmoStartRy, app->gizmoStartRz, R0);
                    MakeWorldRot(1, -dx * 0.35f, Ry);
                    MakeWorldRot(0, -dy * 0.35f, Rx);
                    MatMul4(Ry, R0, R1);
                    MatMul4(Rx, R1, Rnew);
                    EulerFromR(Rnew, so.rx, so.ry, so.rz);
                    if (dx * dx + dy * dy > 4.0f) app->mouseDragged = true;
                } else {
                    const float a0 = std::atan2(app->pressY - app->gizmoScreenPivotY,
                                                app->pressX - app->gizmoScreenPivotX);
                    const float a1 = std::atan2(y - app->gizmoScreenPivotY,
                                                x - app->gizmoScreenPivotX);
                    float dTheta = (a1 - a0) * 180.0f / 3.14159265f;
                    while (dTheta > 180.0f) dTheta -= 360.0f;
                    while (dTheta < -180.0f) dTheta += 360.0f;
                    // Round357：旋转方向随视角修正——环法线朝向相机时屏幕角与右手旋转同向、背离时反向；
                    // 固定符号会让背离相机的轴（默认即蓝轴 Z）旋转反向。s>=0 维持原 -dTheta，s<0 翻转。
                    float view[16];
                    app->camera.ViewMatrix(view);
                    const float toViewer[3] = {view[2], view[6], view[10]};   // 场景→相机方向（世界）
                    const float dirs3[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                    const float s = dirs3[app->gizmoAxis][0] * toViewer[0]
                                  + dirs3[app->gizmoAxis][1] * toViewer[1]
                                  + dirs3[app->gizmoAxis][2] * toViewer[2];
                    const float sign = (s >= 0.0f) ? 1.0f : -1.0f;
                    // Round355：世界空间旋转——绕世界轴 premultiply，gizmo 固定不随物体旋转，朝向不影响拖拽方向
                    float R0[16], Rw[16], Rnew[16];
                    BuildRotFromEuler(app->gizmoStartRx, app->gizmoStartRy, app->gizmoStartRz, R0);
                    MakeWorldRot(app->gizmoAxis, -dTheta * sign, Rw);
                    MatMul4(Rw, R0, Rnew);
                    EulerFromR(Rnew, so.rx, so.ry, so.rz);
                    if (dTheta * dTheta > 4.0f) app->mouseDragged = true;
                }
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;   // Round298：屏蔽视角旋转（旋转物体时不可转视角 BUG 修复）
            }
            // Round304：缩放拖拽（gizmoDragMode==5）——沿轴直线：鼠标位移投影到轴屏幕方向（拖轴=单轴，中心=等比）
            if (app->gizmoDragging && app->gizmoDragMode == 5 &&
                app->selectedObject >= 0 &&
                app->selectedObject < static_cast<int>(app->objects.size())) {
                SceneObject& so = app->objects[app->selectedObject];
                const int axis = app->gizmoAxis;
                float f = 1.0f;
                if (axis == 3) {   // 中心方块：垂直拖动（上=放大）
                    f = 1.0f + (app->pressY - y) * 0.004f;
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
                    const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）
                    const float tip[3] = {gp[0] + dirs[axis][0], gp[1] + dirs[axis][1],
                                          gp[2] + dirs[axis][2]};
                    float sx0, sy0, sx1, sy1;
                    if (ProjectToViewport(mvp, gvp, gp[0], gp[1], gp[2], sx0, sy0) &&
                        ProjectToViewport(mvp, gvp, tip[0], tip[1], tip[2], sx1, sy1)) {
                        const float ddx = sx1 - sx0, ddy = sy1 - sy0;
                        const float dl = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dl > 1e-6f) {
                            const float move = ((x - app->pressX) * ddx + (y - app->pressY) * ddy) / dl;
                            f = 1.0f + move * 0.004f;
                        }
                    }
                }
                if (f > 0.0f) {
                    if (axis == 3) {   // 中心方块：等比（三轴同乘，与旋转无关）
                        so.sx = app->gizmoStartSx * f; so.sy = app->gizmoStartSy * f; so.sz = app->gizmoStartSz * f;
                    } else if (axis >= 0 && axis <= 2) {
                        // Round361：旋转过（斜着）的物体——世界轴缩放**按公式分配**：
                        // 世界轴方向投影到本地坐标 v = R^T·d（R 列 = 本地轴在世界方向），
                        // 按 |v| 分量把缩放因子 f 分配到 sx/sy/sz（sx'=sx·(1+(f-1)|vx|)）。
                        // 未旋转时 v=(1,0,0) 等 → 退化为单轴，与旧行为一致。
                        float R[16];
                        BuildRotFromEuler(so.rx, so.ry, so.rz, R);
                        const float vx = R[axis], vy = R[4 + axis], vz = R[8 + axis];
                        const float k = f - 1.0f;
                        so.sx = app->gizmoStartSx * (1.0f + k * std::fabs(vx));
                        so.sy = app->gizmoStartSy * (1.0f + k * std::fabs(vy));
                        so.sz = app->gizmoStartSz * (1.0f + k * std::fabs(vz));
                    }
                }
                if (std::fabs(f - 1.0f) > 0.002f) app->mouseDragged = true;
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
            // 中心环自由拖拽（Round260）：射线与"过枢轴的视口平面"求交，每帧增量移动（Blender G 自由方向）
            if (app->gizmoDragging && app->gizmoDragMode == 3 &&
                app->selectedObject >= 0 &&
                app->selectedObject < static_cast<int>(app->objects.size())) {
                float o[3], d[3];
                if (BuildViewRay(*app, x, y, o, d)) {
                    float view[16];
                    app->camera.ViewMatrix(view);
                    const float n[3] = {-view[2], -view[6], -view[10]};   // 相机前向 = 平面法线
                    float cur[3];
                    if (RayPlane(o, d, n, app->gizmoPivot, cur)) {
                        if (app->gizmoLastHitValid) {
                            SceneObject& so = app->objects[app->selectedObject];
                            so.tx += cur[0] - app->gizmoLastHit[0];
                            so.ty += cur[1] - app->gizmoLastHit[1];
                            so.tz += cur[2] - app->gizmoLastHit[2];
                            if (dx * dx + dy * dy > 25.0f) app->mouseDragged = true;
                        }
                        app->gizmoLastHit[0] = cur[0];
                        app->gizmoLastHit[1] = cur[1];
                        app->gizmoLastHit[2] = cur[2];
                        app->gizmoLastHitValid = true;
                    }
                }
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
            // 移动三向标拖拽：物体沿锁定轴移动（射线-轴最近点参数 t 的增量）
            if (app->gizmoDragging && app->gizmoAxis >= 0 &&
                app->selectedObject >= 0 &&
                app->selectedObject < static_cast<int>(app->objects.size())) {
                SceneObject& so = app->objects[app->selectedObject];
                const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // Round355：世界轴（gizmo 固定不随物体旋转）
                const float* dir = dirs[app->gizmoAxis];
                float o[3], d[3];
                float t = app->gizmoStartT;
                if (BuildViewRay(*app, x, y, o, d)) ClosestAxisParam(app->gizmoPivot, dir, o, d, t);
                const float delta = t - app->gizmoStartT;
                so.tx = app->gizmoStartTx + dir[0] * delta;
                so.ty = app->gizmoStartTy + dir[1] * delta;
                so.tz = app->gizmoStartTz + dir[2] * delta;
                if (dx * dx + dy * dy > 25.0f) app->mouseDragged = true;
                app->camera.lastX = x;
                app->camera.lastY = y;
                return 0;
            }
            // Round329：框选拖拽更新（左键框选进行中，更新矩形当前角；位移>4px 视为拖拽非点击）
            if (app->marqueeSelecting) {
                app->marqueeX1 = x;
                app->marqueeY1 = y;
                const float mx0 = x - app->marqueeX0, my0 = y - app->marqueeY0;
                if (mx0 * mx0 + my0 * my0 > 16.0f) app->mouseDragged = true;
            }
            // Round332：左键按住且移动>4px → 进入 orbit（纯点击左键走下方拾取）
            if (app->lbuttonDown && !app->camera.orbiting) {
                const float mdx = x - app->camera.lastX, mdy = y - app->camera.lastY;
                if (mdx * mdx + mdy * mdy > 16.0f) app->camera.orbiting = true;
            }
            if (app->camera.orbiting) {
                // Round306：视角拖动状态机——一旦进入拖动即视为拖拽（含慢速拖动），
                // 松开时不再触发物体拾取/取消选择（鼠标划过物体也不影响）
                app->mouseDragged = true;
                // Round310：旋转视角 → 显示左下角万向球
                app->navLastActionMs = GetTickCount64();
                app->navLastActionType = 1;
                app->camera.Orbit(dx, dy);
            } else if (app->camera.panning) {
                app->camera.Pan(dx, dy, static_cast<float>(app->swapchainExtent.height));
                // 渐隐中心由 DrawFrame 每帧按摄像机位置更新，这里无需单独处理
            }
            app->camera.lastX = x;
            app->camera.lastY = y;

            for (auto& b : app->buttons) {
                b.machine.OnMouseMove(PointInButton(b, x, y));
            }
            for (auto& b : app->ballButtons) {
                b.machine.OnMouseMove(PointInButton(b, x, y));
            }
            if (app->menuOpen) {
                for (int i = 0; i < 2; ++i) {
                    app->menuItems[i].machine.OnMouseMove(PointInButton(app->menuItems[i], x, y));
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
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool CreateWindowApp(App& app, HINSTANCE hInstance) {
    const wchar_t kClassName[] = L"VulkanBlankWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        SetError("窗口类注册失败");
        return false;
    }

    RECT rect{0, 0, static_cast<LONG>(kWindowWidth), static_cast<LONG>(kWindowHeight)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowExW(0, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top,
                               nullptr, nullptr, hInstance, nullptr);
    if (!app.hwnd) {
        SetError("窗口创建失败");
        return false;
    }
    SetWindowLongPtrW(app.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    return true;
}

// ---------------------------------------------------------------------------
// Vulkan 实例（apiVersion 1.0 最大兼容老驱动；含可选验证层）
// ---------------------------------------------------------------------------
bool CreateInstance(App& app) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = kAppName.data();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = kAppName.data();
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;  // 兼容：老驱动 / 核显

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> instanceExts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, instanceExts.data());
    for (const auto& ext : instanceExts) {
        if (std::string_view(ext.extensionName) == VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            break;
        }
    }

    std::vector<const char*> layers;
#ifdef VKB_ENABLE_VALIDATION
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    for (const auto& prop : layerProps) {
        if (std::string_view(prop.layerName) == "VK_LAYER_KHRONOS_validation") {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            break;
        }
    }
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    wchar_t forceEnv[16] = {};
    const bool forceSS = GetEnvironmentVariableW(L"AWA_FORCE_SWIFTSHADER", forceEnv, 16) > 0 &&
                         forceEnv[0] == L'1';
    VkResult res = forceSS ? VK_ERROR_INCOMPATIBLE_DRIVER : vkCreateInstance(&createInfo, nullptr, &app.instance);
    // 失败且是「无兼容驱动」时，回退内置软件 ICD（SwiftShader，CPU 渲染）再试一次——
    if (res != VK_SUCCESS && res == VK_ERROR_INCOMPATIBLE_DRIVER && EnableSoftwareIcd()) {
        VkbLog("[createinstance] 硬件 ICD 不可用，已回退 SwiftShader 软件渲染");
        res = vkCreateInstance(&createInfo, nullptr, &app.instance);
    }
    if (res != VK_SUCCESS) {
        SetError(std::string("Vulkan 调用失败: vkCreateInstance (VkResult=") +
                 std::to_string(static_cast<int>(res)) + ")");
        return false;
    }
    if (!LoadInstanceProcs(app.instance)) {
        SetError("实例级 Vulkan 函数加载失败（动态加载）");
        return false;
    }

#ifdef VKB_ENABLE_VALIDATION
    if (g_pfnCreateDebugUtilsMessengerEXT && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT dbgInfo{};
        dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = DebugCallback;
        if (g_pfnCreateDebugUtilsMessengerEXT(app.instance, &dbgInfo, nullptr, &g_debugMessenger) != VK_SUCCESS) {
            g_debugMessenger = VK_NULL_HANDLE;
        }
    }
#endif
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateSurface(App& app, HINSTANCE hInstance) {
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = hInstance;
    surfaceInfo.hwnd = app.hwnd;
    VKB_TRY(g_pfnCreateWin32SurfaceKHR(app.instance, &surfaceInfo, nullptr, &app.surface));
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool PickPhysicalDevice(App& app) {
    wchar_t forceEnv[16] = {};
    const bool forceSS = GetEnvironmentVariableW(L"AWA_FORCE_SWIFTSHADER", forceEnv, 16) > 0 &&
                         forceEnv[0] == L'1';
    if (forceSS) VkbLog("[pdev] AWA_FORCE_SWIFTSHADER=1：只选 CPU 软件渲染设备");

    uint32_t deviceCount = 0;
    VKB_TRY(vkEnumeratePhysicalDevices(app.instance, &deviceCount, nullptr));
    if (deviceCount == 0) {
        SetError("未找到任何 Vulkan 物理设备");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VKB_TRY(vkEnumeratePhysicalDevices(app.instance, &deviceCount, devices.data()));

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties devProps;
        vkGetPhysicalDeviceProperties(dev, &devProps);
        if (forceSS && devProps.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 presentSupport = VK_FALSE;
            VKB_TRY(g_pfnGetPhysicalDeviceSurfaceSupportKHR(dev, i, app.surface, &presentSupport));
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
                app.physicalDevice = dev;
                app.graphicsFamily = i;
                // 检查设备是否支持 4x MSAA 颜色附件（不支持则降级到 1x 防止崩溃）
                const VkSampleCountFlags msaaSupported = devProps.limits.framebufferColorSampleCounts;
                if (app.msaaEnabled && !(msaaSupported & VK_SAMPLE_COUNT_4_BIT)) {
                    app.msaaEnabled = false;
                    app.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
                }
                // **深度格式回退**（核显兼容关键）：VK_FORMAT_D32_SFLOAT 是 Vulkan 1.0
                // vkCreateImage 失败 → 软件打不开。按优先级回退：
                {
                    static const VkFormat kDepthCandidates[] = {
                        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
                    VkFormat chosen = VK_FORMAT_D16_UNORM;
                    for (VkFormat fmt : kDepthCandidates) {
                        VkFormatProperties fp;
                        vkGetPhysicalDeviceFormatProperties(dev, fmt, &fp);
                        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                            chosen = fmt;
                            break;
                        }
                    }
                    if (chosen != app.depthFormat) {
                        app.depthFormat = chosen;
                        VkbLog(("[depth] 深度格式回退: D32_SFLOAT 不支持 → 格式码 " +
                                std::to_string(static_cast<int>(app.depthFormat))).c_str());
                    }
                }
                app.gpuApiVersion = devProps.apiVersion;
                app.gpuType = static_cast<int>(devProps.deviceType);
                app.gpuVendor = devProps.vendorID;
                {
                    // 安全拷贝设备名（最多 255 字符 + NUL；截断时仍保证以 NUL 结尾）
                    size_t n = 0;
                    while (n < sizeof(app.gpuName) - 1 && devProps.deviceName[n]) {
                        app.gpuName[n] = devProps.deviceName[n];
                        ++n;
                    }
                    app.gpuName[n] = '\0';
                }
                VkbLog(("[gpu] " + std::string(app.gpuName) +
                        " api=" + std::to_string(VK_API_VERSION_MAJOR(devProps.apiVersion)) + "." +
                        std::to_string(VK_API_VERSION_MINOR(devProps.apiVersion)) +
                        " type=" + std::to_string(app.gpuType)).c_str());
                return true;
            }
        }
    }

    SetError("未找到支持图形队列并可呈现的物理设备");
    return false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateDevice(App& app) {
    // 探测 Vulkan 1.3 动态渲染能力（老驱动不支持则自动回退传统 render pass）
    bool supportsDynamicRendering = false;
    if (g_pfnGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        g_pfnGetPhysicalDeviceFeatures2(app.physicalDevice, &features2);
        supportsDynamicRendering = (features13.dynamicRendering == VK_TRUE);
    }
    // 核显无硬件 Vulkan 时回退 SwiftShader 后走动态渲染路径会导致 vkCreateDevice/
    if (app.gpuType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        app.useDynamicRendering = false;
        VkbLog("[device] CPU 软件渲染设备：强制传统 render pass 路径（跳过动态渲染）");
    } else {
        app.useDynamicRendering = supportsDynamicRendering;
    }
    VkbLog(("[device] 渲染路径: " + std::string(app.useDynamicRendering ? "动态渲染(1.3)" : "传统 render pass(1.0)")).c_str());

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = app.graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceVulkan13Features enabled13{};
    enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabled13.dynamicRendering = VK_TRUE;

    // Round270：宽线（lineWidth>1）必需 wideLines 特性——线框加粗用（pipelineLine3dWide）
    // Round274：删除 fillModeNonSolid 请求——wire 组（polygonMode=LINE）已删，无管线使用 LINE 模式
    VkPhysicalDeviceFeatures devFeatures{};
    if (g_pfnGetPhysicalDeviceFeatures2) {
        VkPhysicalDeviceFeatures2 f2{};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        g_pfnGetPhysicalDeviceFeatures2(app.physicalDevice, &f2);
        if (f2.features.wideLines == VK_TRUE) devFeatures.wideLines = VK_TRUE;
    } else {
        devFeatures.wideLines = VK_TRUE;   // 无查询接口时假定支持
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = app.useDynamicRendering ? &enabled13 : nullptr;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    deviceInfo.pEnabledFeatures = &devFeatures;

    VKB_TRY(vkCreateDevice(app.physicalDevice, &deviceInfo, nullptr, &app.device));
    vkGetDeviceQueue(app.device, app.graphicsFamily, 0, &app.graphicsQueue);
    if (!LoadDeviceProcs(app.device)) {
        SetError("交换链扩展函数加载失败（动态加载）");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// oldSwapchain：重建时传入旧链句柄（创建成功后才销毁旧链，失败不影响运行）
// ---------------------------------------------------------------------------
bool CreateSwapchain(App& app, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE) {
    VkSurfaceCapabilitiesKHR caps{};
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceCapabilitiesKHR(app.physicalDevice, app.surface, &caps));

    uint32_t formatCount = 0;
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceFormatsKHR(app.physicalDevice, app.surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VKB_TRY(g_pfnGetPhysicalDeviceSurfaceFormatsKHR(app.physicalDevice, app.surface, &formatCount, formats.data()));
    if (formats.empty()) {
        SetError("表面未提供任何颜色格式");
        return false;
    }
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = f;
            break;
        }
    }

    // 呈现模式：**FIFO（垂直同步，双缓冲）**——用户 171 轮"主窗口也创建双缓冲"：
    // 旧逻辑优先 MAILBOX（三缓冲低延迟、无 VSync），帧率波动时图像交替快慢不一，
    // 视觉上像"闪烁/跳动"；FIFO 严格等 VSync 换帧（双缓冲），无撕裂无闪烁最稳。
    // FIFO 是 Vulkan 强制支持的呈现模式，任何平台可用。
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        RECT clientRect{};
        GetClientRect(app.hwnd, &clientRect);
        extent.width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        extent.height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
    }
    app.swapchainExtent = extent;
    app.swapchainFormat = surfaceFormat.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = app.surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = surfaceFormat.format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = oldSwapchain;

    VKB_TRY(g_pfnCreateSwapchainKHR(app.device, &swapInfo, nullptr, &app.swapchain));

    if (oldSwapchain != VK_NULL_HANDLE) {
        for (VkImageView view : app.swapchainImageViews) vkDestroyImageView(app.device, view, nullptr);
        app.swapchainImageViews.clear();
        for (VkFramebuffer fb : app.framebuffers) vkDestroyFramebuffer(app.device, fb, nullptr);
        app.framebuffers.clear();
        g_pfnDestroySwapchainKHR(app.device, oldSwapchain, nullptr);
    }

    uint32_t actualCount = 0;
    VKB_TRY(g_pfnGetSwapchainImagesKHR(app.device, app.swapchain, &actualCount, nullptr));
    app.swapchainImages.resize(actualCount);
    VKB_TRY(g_pfnGetSwapchainImagesKHR(app.device, app.swapchain, &actualCount, app.swapchainImages.data()));

    app.swapchainImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = app.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VKB_TRY(vkCreateImageView(app.device, &viewInfo, nullptr, &app.swapchainImageViews[i]));
    }
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateRenderPass(App& app) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = app.swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = app.depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    VKB_TRY(vkCreateRenderPass(app.device, &rpInfo, nullptr, &app.renderPass));
    return true;
}

bool CreateFramebuffers(App& app) {
    app.framebuffers.resize(app.swapchainImageViews.size());
    for (size_t i = 0; i < app.swapchainImageViews.size(); ++i) {
        VkImageView fbAttachments[2] = {app.swapchainImageViews[i], app.depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = app.renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments;
        fbInfo.width = app.swapchainExtent.width;
        fbInfo.height = app.swapchainExtent.height;
        fbInfo.layers = 1;
        VKB_TRY(vkCreateFramebuffer(app.device, &fbInfo, nullptr, &app.framebuffers[i]));
    }
    return true;
}

bool CreateDepthResources(App& app);
bool CreateMSAAColorResources(App& app);
bool CreateFXAAResources(App& app);
bool RecreateSwapchain(App& app) {
    vkQueueWaitIdle(app.graphicsQueue);
    if (!CreateSwapchain(app, app.swapchain)) return false;
    if (!CreateMSAAColorResources(app)) return false;
    if (!CreateFXAAResources(app)) return false;
    if (!CreateDepthResources(app)) return false;
    // 兼容路径（传统 render pass）需按新尺寸重建 framebuffer
    if (!app.useDynamicRendering && !CreateFramebuffers(app)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateShaderModule(VkDevice device, const unsigned char* code, size_t size, VkShaderModule& module) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(code);
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS;
}

bool CreateVertexBuffer(App& app) {
    const float vertices[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    };
    const VkDeviceSize bufferSize = sizeof(vertices);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = bufferSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKB_TRY(vkCreateBuffer(app.device, &bufInfo, nullptr, &app.vertexBuffer));

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(app.device, app.vertexBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physicalDevice, &memProps);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & want) == want) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        SetError("未找到可用的主机可见内存类型");
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.device, &allocInfo, nullptr, &app.vertexBufferMemory));
    VKB_TRY(vkBindBufferMemory(app.device, app.vertexBuffer, app.vertexBufferMemory, 0));

    void* mapped = nullptr;
    VKB_TRY(vkMapMemory(app.device, app.vertexBufferMemory, 0, bufferSize, 0, &mapped));
    std::memcpy(mapped, vertices, bufferSize);
    vkUnmapMemory(app.device, app.vertexBufferMemory);
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
uint32_t FindMemoryType(const App& app, uint32_t typeFilter, VkMemoryPropertyFlags props);
VkCommandBuffer BeginOneTimeCommand(const App& app);
void EndOneTimeCommand(const App& app, VkCommandBuffer cmd);
bool CreateVertexBuffer3D(App& app) {
    // 未算过 AABB 的物体先算（选中拾取用；导入/复制后自动维护）
    for (auto& o : app.objects)
        if (o.boundsMin[0] > 1e29f) ComputeObjectBounds(o);
    // Round353：wireVerts 已由导入器按原始面生成（四边）。仅当 wireVerts 与 featureVerts 都空（如纯线物体）才用 solid 补建，
    // 否则会清掉已生成的四边 wireVerts（MC 合并网格尤其不能触发，否则对角线又回来）。
    for (auto& o : app.objects)
        if (!o.solidIndices.empty() && o.wireVerts.empty() && o.featureVerts.empty())
            BuildObjectWireframe(o);
    uint64_t totalVerts = 0, totalIndices = 0, totalWireVerts = 0;
    for (auto& o : app.objects) {
        const uint32_t wv = static_cast<uint32_t>(o.wireVerts.size());
        const uint32_t sv = static_cast<uint32_t>(o.solidVerts.size());
        const uint32_t wi = static_cast<uint32_t>(o.wireIndices.size());
        const uint32_t si = static_cast<uint32_t>(o.solidIndices.size());
        o.vertexOffset = static_cast<uint32_t>(totalVerts);
        o.wireVtxOffset = static_cast<uint32_t>(totalWireVerts);   // Round269：专用 40B 缓冲偏移
        o.wireIndexOffset = static_cast<uint32_t>(totalIndices);
        o.solidIndexOffset = static_cast<uint32_t>(totalIndices) + wi;
        totalVerts += static_cast<uint64_t>(wv) + sv;
        totalWireVerts += static_cast<uint64_t>(wv);
        totalIndices += static_cast<uint64_t>(wi) + si;
    }
    if (totalVerts == 0 || totalIndices == 0) return true;

    // 颜色位深模式（用户 159 轮）：GPU buffer 按模式压缩写入颜色
    // 0=无颜色 24B(pos+normal) | 1=16bit half4 32B | 2=8bit uchar4 28B | 3=4bit 26B | 4=1bit 25B
    const int cmode = app.vertexColorMode < 0 ? 0 : (app.vertexColorMode > 4 ? 4 : app.vertexColorMode);
    const VkDeviceSize vtxStride = kColorStride[cmode];
    const VkDeviceSize vertBytes = totalVerts * vtxStride;
    const VkDeviceSize idxBytes = totalIndices * sizeof(uint32_t);

    // 复用已有 buffer（用户 168 轮"导入卡一下"）：容量足够时直接复用，
    // 省 vkDestroyBuffer/vkFreeMemory/vkAllocateMemory/vkBindBuffer 全部开销；
    // 仅在容量不足时才重建（一次性代价，触发一次）
    auto ensureDeviceLocal = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkDeviceSize& capacity, VkBuffer& buf, VkDeviceMemory& mem) -> bool {
        if (buf != VK_NULL_HANDLE && size <= capacity) return true;  // 容量够，直接复用
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(app.device, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(app.device, mem, nullptr); mem = VK_NULL_HANDLE; }
        capacity = 0;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.device, &bi, nullptr, &buf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, buf, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mi == UINT32_MAX) { SetError("未找到可用的显存类型"); return false; }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        VKB_TRY(vkAllocateMemory(app.device, &ai, nullptr, &mem));
        VKB_TRY(vkBindBufferMemory(app.device, buf, mem, 0));
        capacity = size;
        return true;
    };
    if (!ensureDeviceLocal(vertBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           app.vertexBuffer3DCapacity, app.vertexBuffer3D, app.vertexBufferMemory3D)) return false;
    if (!ensureDeviceLocal(idxBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           app.indexBuffer3DCapacity, app.indexBuffer3D, app.indexBufferMemory3D)) return false;
    // Round269：线框专用 40B 顶点缓冲（VertexSolid 原样，供 line3d 管线）
    const VkDeviceSize wireBytes = totalWireVerts * sizeof(VertexSolid);
    if (wireBytes > 0 &&
        !ensureDeviceLocal(wireBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           app.wireVtxBuffer3DCapacity, app.wireVtxBuffer3D, app.wireVtxBufferMemory3D)) return false;

    const VkDeviceSize stageSize = vertBytes + idxBytes + wireBytes;
    VkBuffer stageBuf = VK_NULL_HANDLE;
    VkDeviceMemory stageMem = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = stageSize;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.device, &bi, nullptr, &stageBuf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, stageBuf, &mr);
        const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mi == UINT32_MAX) { SetError("未找到可用的暂存内存类型"); return false; }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = mi;
        VKB_TRY(vkAllocateMemory(app.device, &ai, nullptr, &stageMem));
        VKB_TRY(vkBindBufferMemory(app.device, stageBuf, stageMem, 0));
    }

    void* mapped = nullptr;
    VKB_TRY(vkMapMemory(app.device, stageMem, 0, stageSize, 0, &mapped));
    {
        char* base = static_cast<char*>(mapped);
        size_t vOff = 0, iOff = 0;
        // 按颜色位深逐顶点写入：pos(12) + normal(12) + color(位深压缩)
        const auto copyVerts = [&](const std::vector<VertexSolid>& verts) {
            const size_t n = verts.size();
            if (n == 0) return;
            char* dst = base + vOff;
            for (size_t i = 0; i < n; ++i) {
                const VertexSolid& v = verts[i];
                std::memcpy(dst + i * vtxStride, v.pos, 12);
                std::memcpy(dst + i * vtxStride + 12, v.normal, 12);
                if (cmode == 1) {  // 16bit 半精度
                    uint16_t c[4];
                    for (int j = 0; j < 4; ++j) c[j] = FloatToHalf(v.color[j]);
                    std::memcpy(dst + i * vtxStride + 24, c, 8);
                } else if (cmode == 2) {  // 8bit UNORM
                    uint8_t c[4];
                    for (int j = 0; j < 4; ++j)
                        c[j] = static_cast<uint8_t>(std::clamp(v.color[j], 0.0f, 1.0f) * 255.0f + 0.5f);
                    std::memcpy(dst + i * vtxStride + 24, c, 4);
                } else if (cmode == 3) {  // 4bit 打包 R4G4B4A4（R 低 4 位，小端）
                    uint16_t c = 0;
                    for (int j = 0; j < 4; ++j) {
                        const uint8_t q = static_cast<uint8_t>(std::clamp(v.color[j], 0.0f, 1.0f) * 15.0f + 0.5f);
                        c |= static_cast<uint16_t>(q) << (j * 4);
                    }
                    std::memcpy(dst + i * vtxStride + 24, &c, 2);
                } else if (cmode == 4) {  // 1bit：单字节灰度（RGB 平均 → R8）
                    const float lum = (v.color[0] + v.color[1] + v.color[2]) * (1.0f / 3.0f);
                    dst[i * vtxStride + 24] = static_cast<uint8_t>(std::clamp(lum, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
            vOff += n * vtxStride;
        };
        for (auto& o : app.objects) {
            const size_t wi = o.wireIndices.size(), si = o.solidIndices.size();
            copyVerts(o.wireVerts);
            copyVerts(o.solidVerts);
            if (wi) std::memcpy(base + vertBytes + iOff, o.wireIndices.data(), wi * sizeof(uint32_t));
            iOff += wi * sizeof(uint32_t);
            if (si) std::memcpy(base + vertBytes + iOff, o.solidIndices.data(), si * sizeof(uint32_t));
            iOff += si * sizeof(uint32_t);
        }
        // Round269：线框专用区（40B VertexSolid 原样，从 vertBytes+idxBytes 起）
        if (wireBytes > 0) {
            char* wbase = base + vertBytes + idxBytes;
            size_t wOff = 0;
            for (auto& o : app.objects) {
                const size_t wv = o.wireVerts.size();
                if (wv) {
                    std::memcpy(wbase + wOff, o.wireVerts.data(), wv * sizeof(VertexSolid));
                    wOff += wv * sizeof(VertexSolid);
                }
            }
        }
    }
    vkUnmapMemory(app.device, stageMem);

    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = app.graphicsFamily;
        VKB_TRY(vkCreateCommandPool(app.device, &pci, nullptr, &pool));
        VkCommandBufferAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        a.commandPool = pool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VKB_TRY(vkAllocateCommandBuffers(app.device, &a, &cmd));
        VkCommandBufferBeginInfo b{};
        b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKB_TRY(vkBeginCommandBuffer(cmd, &b));
        VkBufferCopy region{};
        region.srcOffset = 0; region.dstOffset = 0; region.size = vertBytes;
        vkCmdCopyBuffer(cmd, stageBuf, app.vertexBuffer3D, 1, &region);
        region.srcOffset = vertBytes; region.dstOffset = 0; region.size = idxBytes;
        vkCmdCopyBuffer(cmd, stageBuf, app.indexBuffer3D, 1, &region);
        if (wireBytes > 0) {
            region.srcOffset = vertBytes + idxBytes; region.dstOffset = 0; region.size = wireBytes;
            vkCmdCopyBuffer(cmd, stageBuf, app.wireVtxBuffer3D, 1, &region);
        }
        VKB_TRY(vkEndCommandBuffer(cmd));
        VkSubmitInfo s{};
        s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        s.commandBufferCount = 1;
        s.pCommandBuffers = &cmd;
        VKB_TRY(vkQueueSubmit(app.graphicsQueue, 1, &s, VK_NULL_HANDLE));
        VKB_TRY(vkQueueWaitIdle(app.graphicsQueue));
        vkFreeCommandBuffers(app.device, pool, 1, &cmd);
        vkDestroyCommandPool(app.device, pool, nullptr);
    }

    vkDestroyBuffer(app.device, stageBuf, nullptr);
    vkFreeMemory(app.device, stageMem, nullptr);
    return true;
}

struct PushConstants {
    float rect[4];
    float rect2[4];
    float fillColor[4];
    float borderColor[4];
    float cornerRadius;
    float borderWidth;
    float mode;
    float lineHalfWidth;
};
static_assert(sizeof(PushConstants) == 80, "push constant 布局需与 GLSL 一致");

static bool CreateRoundedRectPipeline(App& app, VkPipelineLayout& outLayout, VkPipeline& outPipeline) {
    VkbLog("[pipeline] 开始创建圆角矩形管线");
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kRoundedRectVertSpv, kRoundedRectVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kRoundedRectFragSpv, kRoundedRectFragSpvSize, fragModule)) {
        SetError("着色器模块创建失败");
        return false;
    }
    VkbLog("[pipeline] shader 模块 OK");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    // 深度模板：2D UI 不测试/不写深度。**关键**：传统路径的 renderPass 带 depth 附件，
    // Vulkan 规范要求此时 pDepthStencilState 必须非 NULL——否则部分驱动（SwiftShader）
    // 在 vkCreateGraphicsPipelines 解引用空指针崩溃（核显 SwiftShader 打不开的根因）。
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &outLayout));
    VkbLog("[pipeline] pipelineLayout OK");

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;  // 传统 render pass 有 depth 附件，必须非 NULL
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = outLayout;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;

    VkbLog("[pipeline] vkCreateGraphicsPipelines 前");
    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);
    VkbLog("[pipeline] vkCreateGraphicsPipelines 后");
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}

// Round330：框选矩形半透明管线（panel 同款 shader/layout，blendEnable=TRUE + SRC_ALPHA 混合）
static bool CreatePanelBlendPipeline(App& app, VkPipelineLayout layout, VkPipeline& outPipeline) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kRoundedRectVertSpv, kRoundedRectVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kRoundedRectFragSpv, kRoundedRectFragSpvSize, fragModule)) {
        SetError("着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;
    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline);
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    return res == VK_SUCCESS;
}

bool CreatePipeline(App& app) {
    if (!CreateRoundedRectPipeline(app, app.pipelineLayout, app.pipeline)) return false;
    // Round330：框选矩形半透明管线（panel 同款 shader/layout + SRC_ALPHA 混合）——非致命，失败仅框选填充不透明
    if (!CreatePanelBlendPipeline(app, app.pipelineLayout, app.pipelinePanelBlend)) {
        app.pipelinePanelBlend = VK_NULL_HANDLE;
        VkbLog("[warn] pipelinePanelBlend 创建失败（框选矩形将无半透明）");
    }
    return true;
}

bool CreateMenuPipeline(App& app) {
    return CreateRoundedRectPipeline(app, app.menuPipelineLayout, app.menuPipeline);
}

bool CreatePipelineAxis(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kAxis3dVertSpv, kAxis3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kAxis3dFragSpv, kAxis3dFragSpvSize, fragModule)) {
        SetError("世界坐标轴着色器模块创建失败");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Axis3DPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.pipelineLayoutAxis));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.pipelineLayoutAxis;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.pipelineAxis);
    VKB_TRY(res);

    depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
    res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.pipelineAxisOccluded);
    VKB_TRY(res);

    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateMSAAColorResources(App& app) {
    if (app.msaaColorView != VK_NULL_HANDLE) {
        vkDestroyImageView(app.device, app.msaaColorView, nullptr);
        app.msaaColorView = VK_NULL_HANDLE;
    }
    if (app.msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(app.device, app.msaaColorImage, nullptr);
        app.msaaColorImage = VK_NULL_HANDLE;
    }
    if (app.msaaColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app.device, app.msaaColorMemory, nullptr);
        app.msaaColorMemory = VK_NULL_HANDLE;
    }
    if (!app.msaaEnabled) return true;
    const VkExtent2D& e = app.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.swapchainFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = app.msaaSamples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.device, &imgInfo, nullptr, &app.msaaColorImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.device, app.msaaColorImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physicalDevice, &memProps);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & want) == want) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.device, &allocInfo, nullptr, &app.msaaColorMemory));
    VKB_TRY(vkBindImageMemory(app.device, app.msaaColorImage, app.msaaColorMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = app.msaaColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = app.swapchainFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.device, &viewInfo, nullptr, &app.msaaColorView));
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateFXAAResources(App& app) {
    if (app.fxaaDescriptorSet != VK_NULL_HANDLE) { app.fxaaDescriptorSet = VK_NULL_HANDLE; }
    if (app.fxaaDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app.device, app.fxaaDescriptorPool, nullptr);
        app.fxaaDescriptorPool = VK_NULL_HANDLE;
    }
    // 注意：fxaaDescriptorLayout 不销毁——其内容（1 个 combined image sampler）不随
    // 中间纹理尺寸变化，保留可避免 resize 后 fxaaPipeline 的 pipelineLayout 引用已销毁
    if (app.fxaaSampler != VK_NULL_HANDLE) {
        vkDestroySampler(app.device, app.fxaaSampler, nullptr);
        app.fxaaSampler = VK_NULL_HANDLE;
    }
    if (app.fxaaView != VK_NULL_HANDLE) {
        vkDestroyImageView(app.device, app.fxaaView, nullptr);
        app.fxaaView = VK_NULL_HANDLE;
    }
    if (app.fxaaImage != VK_NULL_HANDLE) {
        vkDestroyImage(app.device, app.fxaaImage, nullptr);
        app.fxaaImage = VK_NULL_HANDLE;
    }
    if (app.fxaaMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app.device, app.fxaaMemory, nullptr);
        app.fxaaMemory = VK_NULL_HANDLE;
    }
    if (app.aaMode != AAMode::FXAA) return true;
    const VkExtent2D& e = app.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.swapchainFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.device, &imgInfo, nullptr, &app.fxaaImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.device, app.fxaaImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physicalDevice, &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.device, &allocInfo, nullptr, &app.fxaaMemory));
    VKB_TRY(vkBindImageMemory(app.device, app.fxaaImage, app.fxaaMemory, 0));

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = app.fxaaImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = app.swapchainFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.device, &vi, nullptr, &app.fxaaView));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VKB_TRY(vkCreateSampler(app.device, &si, nullptr, &app.fxaaSampler));

    // descriptor set layout：内容不随尺寸变化，首次创建后复用（避免 resize 后失效）
    if (app.fxaaDescriptorLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{};
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.bindingCount = 1;
        dl.pBindings = &binding;
        VKB_TRY(vkCreateDescriptorSetLayout(app.device, &dl, nullptr, &app.fxaaDescriptorLayout));
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &poolSize;
    VKB_TRY(vkCreateDescriptorPool(app.device, &dp, nullptr, &app.fxaaDescriptorPool));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.fxaaDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.fxaaDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.device, &da, &app.fxaaDescriptorSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.fxaaSampler;
    dii.imageView = app.fxaaView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = app.fxaaDescriptorSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.device, 1, &wd, 0, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreatePipelineFXAA(App& app) {
    if (app.aaMode != AAMode::FXAA) return true;
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kFxaaVertSpv, kFxaaVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kFxaaFragSpv, kFxaaFragSpvSize, fragModule)) {
        SetError("FXAA 着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度模板：后处理不测试/不写深度；传统 render pass 有 depth 附件必须非 NULL（同 rounded_rect）
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 2 * sizeof(float);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &app.fxaaDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.fxaaPipelineLayout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;  // 传统 render pass 有 depth 附件，必须非 NULL
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.fxaaPipelineLayout;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                             nullptr, &app.fxaaPipeline);
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateDepthResources(App& app) {
    if (app.depthImage != VK_NULL_HANDLE) {
        vkDestroyImageView(app.device, app.depthView, nullptr);
        vkDestroyImage(app.device, app.depthImage, nullptr);
        vkFreeMemory(app.device, app.depthMemory, nullptr);
        app.depthView = VK_NULL_HANDLE;
        app.depthImage = VK_NULL_HANDLE;
        app.depthMemory = VK_NULL_HANDLE;
    }
    const VkExtent2D& e = app.swapchainExtent;
    if (e.width == 0 || e.height == 0) return false;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = app.depthFormat;
    imgInfo.extent = {e.width, e.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = app.msaaSamples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKB_TRY(vkCreateImage(app.device, &imgInfo, nullptr, &app.depthImage));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(app.device, app.depthImage, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physicalDevice, &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) { SetError("无设备本地内存"); return false; }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;
    VKB_TRY(vkAllocateMemory(app.device, &allocInfo, nullptr, &app.depthMemory));
    VKB_TRY(vkBindImageMemory(app.device, app.depthImage, app.depthMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = app.depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = app.depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.device, &viewInfo, nullptr, &app.depthView));
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreatePipelineGrid(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kGridVertSpv, kGridVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kGridFragSpv, kGridFragSpvSize, fragModule)) {
        SetError("网格着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 160;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.grid.layout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.grid.layout;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.grid.pipeline);
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreatePipelineSolid(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kUnified3dVertSpv, kUnified3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kUnified3dFragSpv, kUnified3dFragSpvSize, fragModule)) {
        SetError("实体着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // 颜色位深动态切换（用户 159 轮）：5 种管线 —— 无颜色 / 16bit / 8bit / 4bit / 1bit
    // 顶点布局：pos(12) + normal(12) + color(位深决定)；无颜色模式只读 pos+normal
    VkVertexInputAttributeDescription attrsNoColor[2]{};
    attrsNoColor[0].location = 0; attrsNoColor[0].binding = 0;
    attrsNoColor[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrsNoColor[0].offset = 0;
    attrsNoColor[1].location = 1; attrsNoColor[1].binding = 0;
    attrsNoColor[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrsNoColor[1].offset = 12;

    VkVertexInputAttributeDescription attrsColor[5][3]{};
    for (int m = 1; m <= 4; ++m) {
        attrsColor[m][0].location = 0; attrsColor[m][0].binding = 0;
        attrsColor[m][0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrsColor[m][0].offset = 0;
        attrsColor[m][1].location = 1; attrsColor[m][1].binding = 0;
        attrsColor[m][1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrsColor[m][1].offset = 12;
        attrsColor[m][2].location = 2; attrsColor[m][2].binding = 0;
        attrsColor[m][2].offset = 24;
        // 16bit=半精度 float4；8bit=UNORM uchar4；4bit=打包 4×4bit；1bit=单字节灰度（R8）
        attrsColor[m][2].format =
            (m == 1) ? VK_FORMAT_R16G16B16A16_SFLOAT :
            (m == 2) ? VK_FORMAT_R8G8B8A8_UNORM :
            (m == 3) ? VK_FORMAT_R4G4B4A4_UNORM_PACK16 :
                       VK_FORMAT_R8_UNORM;
    }
    // 各模式的顶点 stride：24（无颜色）/ 32 / 28 / 26 / 25
    // （全局 kColorStride 定义于 VertexSolid 之后）

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Push3D);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.pipelineLayoutSolid));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.depthFormat;

    auto createSolid = [&](uint32_t stride, const VkVertexInputAttributeDescription* attrs,
                           uint32_t attrCount, VkPipeline& out) -> bool {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = stride;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = attrCount;
        vertexInput.pVertexAttributeDescriptions = attrs;

        VkPipelineRasterizationStateCreateInfo ras = rasterizer;
        ras.polygonMode = VK_POLYGON_MODE_FILL;
        VkPipelineMultisampleStateCreateInfo ms = multisample;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &ras;
        pipelineInfo.pMultisampleState = &ms;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = app.pipelineLayoutSolid;
        pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out) == VK_SUCCESS;
    };
    // 实体组（FILL）
    if (!createSolid(kColorStride[0], attrsNoColor, 2, app.pipelineSolidNoColor) ||
        !createSolid(kColorStride[1], attrsColor[1], 3, app.pipelineSolid) ||
        !createSolid(kColorStride[2], attrsColor[2], 3, app.pipelineSolid8) ||
        !createSolid(kColorStride[3], attrsColor[3], 3, app.pipelineSolid4) ||
        !createSolid(kColorStride[4], attrsColor[4], 3, app.pipelineSolid1)) {
        SetError("实体管线创建失败（颜色位深 5 模式）");
        vkDestroyShaderModule(app.device, vertModule, nullptr);
        vkDestroyShaderModule(app.device, fragModule, nullptr);
        return false;
    }
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    return true;
}

// 3D 线框管线（LINE_LIST；选中物体高亮框 / Tab 线框预览，Round237）
// line3d.vert: inPosition(vec3) + inColor(vec4)，push constant = mvp(64B)
bool CreatePipelineLine3d(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kLine3dVertSpv, kLine3dVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kLine3dFragSpv, kLine3dFragSpvSize, fragModule)) {
        SetError("3D 线框着色器模块创建失败");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // VertexSolid 布局：pos(0) + normal(12) + color(24)，stride 40；只读 pos 和 color
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 40;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = 24;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    // 深度：测试开（LEQUAL 保证线不被同深度面遮挡）、不写深度
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 64;   // 仅 mvp

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.pipelineLayoutLine3d));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;
    renderingInfo.depthAttachmentFormat = app.depthFormat;

    // 共用同一布局；变体：
    //   pipelineLine3d         LINE_LIST  depthTest=TRUE  lineWidth=1（高亮框/线框预览）
    //   pipelineLine3dWide     LINE_LIST  depthTest=TRUE  lineWidth=2（Round270：线框模式加粗，Blender 边框）
    //   pipelineLine3dNoDepth  LINE_LIST  depthTest=FALSE（三向标线框：最顶层始终可见）
    //   pipelineGizmoSolid     TRIANGLE_LIST depthTest=FALSE（三向标锥头实体填充，Round244）
    const auto makePipe = [&](VkBool32 depthTest, VkBool32 depthWrite,
                              VkPrimitiveTopology topo, float lineW, VkPipeline& out) -> bool {
        VkPipelineDepthStencilStateCreateInfo ds = depthStencil;
        ds.depthTestEnable = depthTest;
        ds.depthWriteEnable = depthWrite;
        VkPipelineInputAssemblyStateCreateInfo ia = inputAssembly;
        ia.topology = topo;
        VkPipelineRasterizationStateCreateInfo ras = rasterizer;
        ras.lineWidth = lineW;
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &ia;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &ras;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &ds;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = app.pipelineLayoutLine3d;
        pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
        pipelineInfo.subpass = 0;
        return vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out) == VK_SUCCESS;
    };
    // Round270 fix：宽线管线（lineWidth=2）创建非致命——部分驱动/硬件不支持宽线，
    // 失败时保留 NULL，DrawFrame 已有回退到 1px line3d 管线的逻辑
    const bool ok =
        makePipe(VK_TRUE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1.0f, app.pipelineLine3d) &&
        makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1.0f, app.pipelineLine3dNoDepth) &&
        makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 1.0f, app.pipelineGizmoSolid);
    // 宽线独立尝试（失败不影响 ok）
    if (ok && !makePipe(VK_TRUE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 2.0f, app.pipelineLine3dWide)) {
        app.pipelineLine3dWide = VK_NULL_HANDLE;   // 显式置空（makePipe 失败时不保证清零）
        VkbLog("[warn] pipelineLine3dWide 创建失败（wideLines 不支持或驱动限制），回退 1px");
    }
    // Round305：无深度宽线（旋转 gizmo 环 2px）——失败回退 1px 无深度管线
    if (ok && !makePipe(VK_FALSE, VK_FALSE, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 2.0f, app.pipelineLine3dNoDepthWide)) {
        app.pipelineLine3dNoDepthWide = VK_NULL_HANDLE;
        VkbLog("[warn] pipelineLine3dNoDepthWide 创建失败，旋转环回退 1px");
    }
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    return ok;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool CreateCommandResources(App& app) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = app.graphicsFamily;
    VKB_TRY(vkCreateCommandPool(app.device, &poolInfo, nullptr, &app.commandPool));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = app.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VKB_TRY(vkAllocateCommandBuffers(app.device, &allocInfo, &app.commandBuffer));

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VKB_TRY(vkCreateSemaphore(app.device, &semInfo, nullptr, &app.imageAvailable));
    VKB_TRY(vkCreateSemaphore(app.device, &semInfo, nullptr, &app.renderFinished));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VKB_TRY(vkCreateFence(app.device, &fenceInfo, nullptr, &app.inFlightFence));
    return true;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
struct PanelSpec {
    VkRect2D rect;
    VkClearColorValue fill;
    float radius;
    VkClearColorValue border = kBorderColor;
    float borderWidth = kLineWidth;   // Round248：可关闭描边（0=不画，去白边）
};

void DrawPanel(App& app, const PanelSpec& panel) {
    vkCmdSetScissor(app.commandBuffer, 0, 1, &panel.rect);

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

    vkCmdPushConstants(app.commandBuffer, app.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);
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
    vkCmdSetScissor(app.commandBuffer, 0, 1, &scissor);

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

    vkCmdPushConstants(app.commandBuffer, app.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void ImageBarrierAspect(App& app, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                        VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(app.commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void ImageBarrier(App& app, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                  VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    ImageBarrierAspect(app, image, VK_IMAGE_ASPECT_COLOR_BIT, oldLayout, newLayout,
                       srcStage, srcAccess, dstStage, dstAccess);
}

// （动态渲染要求图像在指定布局；动态渲染路径不自动转换，必须显式屏障）
void TransitionBeforeRender(App& app, VkImage image) {
    ImageBarrier(app, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void TransitionAfterRender(App& app, VkImage image) {
    ImageBarrier(app, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

struct TextPush {
    float rect[4];
    float color[4];
};
static_assert(sizeof(TextPush) == 32, "TextPush 布局需与 text shader 一致");

uint32_t FindMemoryType(const App& app, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return UINT32_MAX;
}

void CmdImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
                     VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkCommandBuffer BeginOneTimeCommand(const App& app) {
    VkCommandBufferAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    a.commandPool = app.commandPool;
    a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    a.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(app.device, &a, &cmd);
    VkCommandBufferBeginInfo b{};
    b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &b);
    return cmd;
}

void EndOneTimeCommand(const App& app, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    s.commandBufferCount = 1;
    s.pCommandBuffers = &cmd;
    vkQueueSubmit(app.graphicsQueue, 1, &s, VK_NULL_HANDLE);
    vkQueueWaitIdle(app.graphicsQueue);
    vkFreeCommandBuffers(app.device, app.commandPool, 1, &cmd);
}

[[maybe_unused]] bool RasterizeText(const wchar_t* text, int fontSize, int pad, const wchar_t* fontName,
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
                  int targetW = 0, int targetH = 0) {
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
    // Round277：可选 WIC 高质量缩放（球按钮图标 2160×2160 → 128×128，显存与纹理上传都省）
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

bool CreateTextResources(App& app, const std::vector<uint8_t>& rgba, int w, int h) {
    app.textWidth = w;
    app.textHeight = h;
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.device, &b, nullptr, &stagingBuffer));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { SetError("文字纹理：找不到主机可见内存"); return false; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        VKB_TRY(vkAllocateMemory(app.device, &a, nullptr, &stagingMemory));
        VKB_TRY(vkBindBufferMemory(app.device, stagingBuffer, stagingMemory, 0));
        void* data = nullptr;
        VKB_TRY(vkMapMemory(app.device, stagingMemory, 0, imageSize, 0, &data));
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.device, stagingMemory);
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
    VKB_TRY(vkCreateImage(app.device, &img, nullptr, &app.textImage));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(app.device, app.textImage, &mr);
    const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (idx == UINT32_MAX) { SetError("文字纹理：找不到设备本地内存"); return false; }
    VkMemoryAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = mr.size;
    a.memoryTypeIndex = idx;
    VKB_TRY(vkAllocateMemory(app.device, &a, nullptr, &app.textMemory));
    VKB_TRY(vkBindImageMemory(app.device, app.textImage, app.textMemory, 0));

    {
        VkCommandBuffer cmd = BeginOneTimeCommand(app);
        CmdImageBarrier(cmd, app.textImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, app.textImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        CmdImageBarrier(cmd, app.textImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        EndOneTimeCommand(app, cmd);
    }
    vkDestroyBuffer(app.device, stagingBuffer, nullptr);
    vkFreeMemory(app.device, stagingMemory, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = app.textImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.device, &vi, nullptr, &app.textView));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VKB_TRY(vkCreateSampler(app.device, &si, nullptr, &app.textSampler));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    VKB_TRY(vkCreateDescriptorSetLayout(app.device, &dl, nullptr, &app.textDescriptorLayout));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 16;   // Round297：8→16（球3 + 变换3 共 6 个新图标）
    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 16;  // 支持 16 个 descriptor set（球/变换/顶栏图标预留充足）
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &poolSize;
    VKB_TRY(vkCreateDescriptorPool(app.device, &dp, nullptr, &app.textDescriptorPool));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.textDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.textDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.device, &da, &app.textDescriptorSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.textSampler;
    dii.imageView = app.textView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = app.textDescriptorSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.device, 1, &wd, 0, nullptr);
    return true;
}

bool CreateIconTexture(App& app, const std::vector<uint8_t>& rgba, int w, int h,
                       VkImage& outImage, VkDeviceMemory& outMemory,
                       VkImageView& outView, VkDescriptorSet& outSet) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKB_TRY(vkCreateBuffer(app.device, &b, nullptr, &stagingBuffer));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { SetError("图标纹理：找不到主机可见内存"); return false; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        VKB_TRY(vkAllocateMemory(app.device, &a, nullptr, &stagingMemory));
        VKB_TRY(vkBindBufferMemory(app.device, stagingBuffer, stagingMemory, 0));
        void* data = nullptr;
        VKB_TRY(vkMapMemory(app.device, stagingMemory, 0, imageSize, 0, &data));
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.device, stagingMemory);
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
    VKB_TRY(vkCreateImage(app.device, &img, nullptr, &outImage));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(app.device, outImage, &mr);
    const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (idx == UINT32_MAX) { SetError("图标纹理：找不到设备本地内存"); return false; }
    VkMemoryAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = mr.size;
    a.memoryTypeIndex = idx;
    VKB_TRY(vkAllocateMemory(app.device, &a, nullptr, &outMemory));
    VKB_TRY(vkBindImageMemory(app.device, outImage, outMemory, 0));

    {
        VkCommandBuffer cmd = BeginOneTimeCommand(app);
        CmdImageBarrier(cmd, outImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, outImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        CmdImageBarrier(cmd, outImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        EndOneTimeCommand(app, cmd);
    }
    vkDestroyBuffer(app.device, stagingBuffer, nullptr);
    vkFreeMemory(app.device, stagingMemory, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = outImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKB_TRY(vkCreateImageView(app.device, &vi, nullptr, &outView));

    VkDescriptorSetAllocateInfo da{};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = app.textDescriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &app.textDescriptorLayout;
    VKB_TRY(vkAllocateDescriptorSets(app.device, &da, &outSet));

    VkDescriptorImageInfo dii{};
    dii.sampler = app.textSampler;
    dii.imageView = outView;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = outSet;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.device, 1, &wd, 0, nullptr);
    return true;
}

// Round277：加载左上角 3 球按钮图标（嵌入 RCDATA 1/2/3.png → WIC 缩放到 128×128 → 纹理）
// 失败降级：与齿轮/笔/导入/导出图标一致——仅 log 警告，ballIcons[i].valid=false → DrawLogicBar 退回纯色圆
bool LoadBallIcons(App& app) {
    // Round297：前 3 = 球按钮（1/2/3 线框/实体/渲染），后 3 = 顶栏变换按钮（4/5/6 移动/旋转/缩放）
    static const WORD kIconResIds[6] = {ID_BTN_ICON_1, ID_BTN_ICON_2, ID_BTN_ICON_3,
                                        ID_BTN_ICON_4, ID_BTN_ICON_5, ID_BTN_ICON_6};
    constexpr int kBallTexSize = 128;
    const HMODULE hMod = GetModuleHandleW(nullptr);  // 当前 exe 句柄
    char msg[256];
    for (int i = 0; i < 6; ++i) {
        App::BallIconImage& dst = (i < 3) ? app.ballIcons[i] : app.transformIcons[i - 3];
        // FindResourceA 接受 LPSTR：资源 ID 用 MAKEINTRESOURCE(数字) 走"整型 ID"分支，类型 RT_RCDATA 走"预定义资源类型"分支
        HRSRC hRes = FindResourceA(hMod, MAKEINTRESOURCE(kIconResIds[i]), RT_RCDATA);
        if (!hRes) {
            std::snprintf(msg, sizeof(msg), "[icon] FindResource ID %d 失败", kIconResIds[i]);
            VkbLog(msg);
            continue;
        }
        HGLOBAL hLoaded = LoadResource(hMod, hRes);
        if (!hLoaded) continue;
        const void* data = LockResource(hLoaded);
        const DWORD size = SizeofResource(hMod, hRes);
        if (!data || size == 0) continue;

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePngWic(static_cast<const unsigned char*>(data), size, rgba, w, h,
                          kBallTexSize, kBallTexSize)) {
            std::snprintf(msg, sizeof(msg), "[ballicon] WIC 解码图标 %d 失败", i);
            VkbLog(msg);
            continue;
        }
        // Round279：球按钮图标圆形裁剪（圆外 alpha=0）——叠在圆形按钮上，透明角露出按钮底色；
        // 变换按钮（i>=3）是顶栏方形按钮，保持方形不裁剪
        if (i < 3) {
            const float cx = w * 0.5f, cy = h * 0.5f;
            const float r = std::min(w, h) * 0.5f - 1.0f;   // 半径略缩 1px 抗锯齿
            const float r2 = r * r;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    if (dx * dx + dy * dy > r2)
                        rgba[static_cast<size_t>(y * w + x) * 4 + 3] = 0;
                }
        }
        if (!CreateIconTexture(app, rgba, w, h,
                               dst.image, dst.memory,
                               dst.view, dst.set)) {
            std::snprintf(msg, sizeof(msg), "[icon] 图标 %d 纹理创建失败: %s", i, g_error.c_str());
            VkbLog(msg);
            g_error.clear();
            continue;
        }
        dst.w = static_cast<uint32_t>(w);
        dst.h = static_cast<uint32_t>(h);
        dst.valid = true;
        std::snprintf(msg, sizeof(msg), "[icon] 图标 %d OK %dx%d", i, w, h);
        VkbLog(msg);
    }
    return true;  // 降级语义：单图失败不影响其他/整体启动
}

bool CreateTextPipeline(App& app) {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (!CreateShaderModule(app.device, kTextVertSpv, kTextVertSpvSize, vertModule) ||
        !CreateShaderModule(app.device, kTextFragSpv, kTextFragSpvSize, fragModule)) {
        SetError("文字着色器模块创建失败");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 2 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = app.msaaSamples;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(TextPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &app.textDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VKB_TRY(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.textPipelineLayout));

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &app.swapchainFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = app.useDynamicRendering ? &renderingInfo : nullptr;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = app.textPipelineLayout;
    pipelineInfo.renderPass = app.useDynamicRendering ? VK_NULL_HANDLE : app.renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &app.textPipeline);
    vkDestroyShaderModule(app.device, vertModule, nullptr);
    vkDestroyShaderModule(app.device, fragModule, nullptr);
    VKB_TRY(res);
    return true;
}

void DrawIcon(App& app, const VkRect2D& iconRect, VkClearColorValue color, VkDescriptorSet set) {
    if (app.textPipeline == VK_NULL_HANDLE || set == VK_NULL_HANDLE) return;  // 纹理降级保护
    vkCmdSetScissor(app.commandBuffer, 0, 1, &iconRect);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.textPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &off);
    vkCmdBindDescriptorSets(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            app.textPipelineLayout, 0, 1, &set, 0, nullptr);
    TextPush tp{};
    tp.rect[0] = static_cast<float>(iconRect.offset.x);
    tp.rect[1] = static_cast<float>(iconRect.offset.y);
    tp.rect[2] = static_cast<float>(iconRect.extent.width);
    tp.rect[3] = static_cast<float>(iconRect.extent.height);
    tp.color[0] = color.float32[0];
    tp.color[1] = color.float32[1];
    tp.color[2] = color.float32[2];
    tp.color[3] = color.float32[3];
    vkCmdPushConstants(app.commandBuffer, app.textPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TextPush), &tp);
    vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void DrawMenu(App& app) {
    if (app.menuPipeline == VK_NULL_HANDLE) return;
    constexpr float kMenuPad = 10.0f;
    constexpr float kMenuItemH = 30.0f;
    const float fullH = kMenuPad * 2.0f + kMenuItemH;
    // 底部白线 BUG 修复：menuAnim 接近 1 时 showH 截断（static_cast<uint32_t>）
    const float showH = (app.menuAnim >= 0.999f) ? fullH : (fullH * app.menuAnim);
    if (showH < 1.0f) return;

    const float mx = static_cast<float>(app.menuRect.offset.x);
    const float my = static_cast<float>(app.menuRect.offset.y);
    const float mw = static_cast<float>(app.menuRect.extent.width);
    VkDeviceSize off = 0;
    const VkRect2D menuClip{
        {app.menuRect.offset.x, app.menuRect.offset.y},
        {app.menuRect.extent.width, static_cast<uint32_t>(std::ceil(showH))}};

    vkCmdSetScissor(app.commandBuffer, 0, 1, &menuClip);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.menuPipeline);
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &off);

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
    vkCmdPushConstants(app.commandBuffer, app.menuPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);

    //   套用三态动画（用 menuItems[i] 的过渡色/边框色绘制）；菜单展开到一定程度后才画（避免闪烁）
    if (app.menuAnim > 0.2f) {
        const float iconSize = kMenuItemH - 2.0f;

        for (int i = 0; i < 2; ++i) {
            const UiButton& btn = app.menuItems[i];
            const VkRect2D itemRect = btn.rect;

            // 重新绑定菜单管线 + 顶点缓冲（上一个按钮的 DrawIcon 已切到 textPipeline，必须重绑）
            vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.menuPipeline);
            vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &off);
            vkCmdSetScissor(app.commandBuffer, 0, 1, &itemRect);

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
            vkCmdPushConstants(app.commandBuffer, app.menuPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(itemPush), &itemPush);
            vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);

            const VkDescriptorSet set = (i == 0) ? app.importDescriptorSet : app.exportDescriptorSet;
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

// ==================== 3D 视口比例尺（用户 2026-08-20：左下角，表示当前摄像机缩放大小）====================

// 紧凑格式化世界长度（避免科学计数法）：按量级保留 0~2 位小数
static void FormatScaleLen(float len, char* buf, int n) {
    if (len >= 100.0f)      std::snprintf(buf, static_cast<size_t>(n), "%.0f", len);
    else if (len >= 10.0f)  std::snprintf(buf, static_cast<size_t>(n), "%.0f", len);
    else if (len >= 1.0f)   std::snprintf(buf, static_cast<size_t>(n), "%.1f", len);
    else                    std::snprintf(buf, static_cast<size_t>(n), "%.2f", len);
}

// Round331：缩放条数字用 RasterizeText+UploadLabelRgba（系统字体）——前向声明（定义在其后）
static void UploadLabelRgba(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h);

// 比例尺（Round256）：**固定长条 + 移动竖线**——横条长度固定，竖线按相机距离
// （对数映射，范围 0.3~10000 与相机 zoom clamp 一致）左右移动指示当前缩放；
// 条上方居中显示当前距离数值（RasterizeText 系统字体，窄字 + 明显字间距）
static void DrawScaleBar(App& app, const Layout& layout) {
    // Round328：淡入淡出动画（navZoomAlpha 驱动）——默认隐藏；滚轮缩放 0.1s 淡入、
    // 停止 0.5s 淡出、与罗盘/距离条 0.1s 互斥替换（UpdateNavHud 精确时间规则）
    if (app.navZoomAlpha <= 0.004f) return;
    const float za = app.navZoomAlpha;
    const VkRect2D& vp = layout.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return;

    const Vec3 r{app.camera.position.x - app.camera.target.x,
                 app.camera.position.y - app.camera.target.y,
                 app.camera.position.z - app.camera.target.z};
    const float dist = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (dist < 1e-4f) return;

    // 竖线位置：相机距离对数映射到固定条（近→左，远→右，平滑移动）
    constexpr float kMinDist = 0.3f;
    constexpr float kMaxDist = 1500.0f;   // Round257：与相机缩放最大值一致（10000→1500）
    const float lo = std::log10(kMinDist), hi = std::log10(kMaxDist);
    const float t = std::max(0.0f, std::min(1.0f, (std::log10(dist) - lo) / (hi - lo)));

    constexpr float kBarW = 140.0f;   // 固定条长（不随缩放变化）
    const float margin = 16.0f;
    const float baseY = static_cast<float>(vp.offset.y + vp.extent.height) - margin;
    const float x0 = static_cast<float>(vp.offset.x) + margin;
    const float x1 = x0 + kBarW;
    const float tick = 5.0f;
    const VkClearColorValue trackCol{0.60f * za, 0.60f * za, 0.64f * za, za};   // 轨道（稍暗，颜色调制淡入淡出）
    const VkClearColorValue markCol{0.92f * za, 0.92f * za, 0.92f * za, za};    // 竖线/端刻线/数字

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.swapchainExtent.width),
                        static_cast<float>(app.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
    VkDeviceSize voff = 0;
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &voff);

    // 主横线（固定长，Round331：整体变细）
    DrawLine(app, vp, x0, baseY, x1, baseY, trackCol, 0.7f);
    // 两端竖刻线
    DrawLine(app, vp, x0, baseY - tick, x0, baseY + tick, markCol, 0.7f);
    DrawLine(app, vp, x1, baseY - tick, x1, baseY + tick, markCol, 0.7f);
    // 中间小刻度（25% / 50% / 75%）
    for (int k = 1; k <= 3; ++k) {
        const float sx = x0 + kBarW * (0.25f * k);
        DrawLine(app, vp, sx, baseY - 3.0f, sx, baseY + 3.0f, trackCol, 0.5f);
    }
    // 移动竖线（指示当前缩放，比轨道稍亮；Round331：变细）
    const float ix = x0 + t * kBarW;
    DrawLine(app, vp, ix, baseY - 8.0f, ix, baseY + 8.0f, markCol, 1.0f);

    // 数字标签（条上方居中）：系统默认字体（Segoe UI），Round331 替换七段数码
    if (app.textPipeline != VK_NULL_HANDLE) {
        char buf[32];
        FormatScaleLen(dist, buf, sizeof(buf));
        wchar_t wbuf[32];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 32);
        if (app.scaleLabel.text != wbuf) {
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(wbuf, 13, 2, L"Segoe UI", rgba, tw, th)) {
                UploadLabelRgba(app, app.scaleLabel, rgba, tw, th);
                app.scaleLabel.text = wbuf;
            }
        }
        if (app.scaleLabel.set != VK_NULL_HANDLE) {
            const float wTxt = static_cast<float>(app.scaleLabel.w);
            const float hTxt = static_cast<float>(app.scaleLabel.h);
            const float lx = x0 + (kBarW - wTxt) * 0.5f;
            const float ly = baseY - tick - hTxt - 4.0f;
            const VkRect2D tRect{{static_cast<int32_t>(lx), static_cast<int32_t>(ly)},
                                 {static_cast<uint32_t>(wTxt), static_cast<uint32_t>(hTxt)}};
            VkClearColorValue col{{markCol.float32[0], markCol.float32[1], markCol.float32[2], za}};
            DrawIcon(app, tRect, col, app.scaleLabel.set);
        }
    }
}

// 摄像机坐标显示（Round345）：平移时左下角显示 X/Y/Z 三行坐标（3 位精度）。
// Round347：显示相机注视点 target（而非 eye position）——turntable 旋转/缩放都保持 target 固定，
// 只有平移(pan) 才改变它，故旋转视角时坐标不变；由 navCoordAlpha 淡入淡出（目标点变化→淡入，静止 0.5s 淡出）。
static void DrawCoordHud(App& app, const Layout& layout) {
    if (app.navCoordAlpha <= 0.004f) return;
    const float ca = app.navCoordAlpha;
    const VkRect2D& vp = layout.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return;
    const Vec3 p = app.camera.target;   // Round347：注视点（旋转/缩放不变，仅平移变）

    // 全屏视口（DrawIcon 依赖当前视口把像素坐标映射到屏幕）
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.swapchainExtent.width),
                        static_cast<float>(app.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);

    const float margin = 16.0f;
    const float baseY = static_cast<float>(vp.offset.y + vp.extent.height) - margin;
    const float x0 = static_cast<float>(vp.offset.x) + margin + 4.0f;
    const float lineH = 18.0f;
    const char* axes[3] = {"X", "Y", "Z"};
    const float vals[3] = {p.x, p.y, p.z};

    for (int i = 0; i < 3; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %.3f", axes[i], vals[i]);
        wchar_t wbuf[64];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 64);
        if (app.coordLabels[i].text != wbuf) {
            std::vector<uint8_t> rgba;
            int tw = 0, th = 0;
            if (RasterizeText(wbuf, 13, 2, L"Segoe UI", rgba, tw, th)) {
                UploadLabelRgba(app, app.coordLabels[i], rgba, tw, th);
                app.coordLabels[i].text = wbuf;
            }
        }
        if (app.coordLabels[i].set != VK_NULL_HANDLE) {
            const float wTxt = static_cast<float>(app.coordLabels[i].w);
            const float hTxt = static_cast<float>(app.coordLabels[i].h);
            const float lx = x0;
            const float ly = baseY - 24.0f - (2 - i) * lineH;   // Round352：整体下移 20px（X 最上、Z 最下）
            const VkRect2D tRect{{static_cast<int32_t>(lx), static_cast<int32_t>(ly)},
                                 {static_cast<uint32_t>(wTxt), static_cast<uint32_t>(hTxt)}};
            VkClearColorValue col{{0.90f * ca, 0.90f * ca, 0.92f * ca, ca}};
            DrawIcon(app, tRect, col, app.coordLabels[i].set);
        }
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 3D 视口左上角 3 个圆形按钮（Round248：标准按钮动画）：矩形随 3D 视口布局每帧同步，
// 首次绘制时按按钮主题初始化颜色
static void UpdateBallButtons(App& app, const Layout& layout) {
    const VkRect2D& bvp = layout.viewport;
    // Round286：按钮随窗口缩小而缩小（最多缩到默认的 1/2）——以首次视口尺寸为基准
    if (app.ballRefW == 0 || app.ballRefH == 0) {
        app.ballRefW = bvp.extent.width;
        app.ballRefH = bvp.extent.height;
    }
    float scale = 1.0f;
    if (app.ballRefW > 0 && app.ballRefH > 0 && bvp.extent.width > 0 && bvp.extent.height > 0) {
        scale = std::clamp(std::min(static_cast<float>(bvp.extent.width) / app.ballRefW,
                                    static_cast<float>(bvp.extent.height) / app.ballRefH),
                           0.5f, 1.0f);   // 最小 0.5（默认的一半），最大 1.0（不放大）
    }
    const float kBallR = 20.0f * scale;   // Round284：默认半径 20（15+5）；Round286：随窗口缩放
    const float kBallGap = 10.0f * scale;   // 间距随缩放等比
    const float pitch = 2.0f * kBallR + kBallGap;
    const float bx0 = static_cast<float>(bvp.offset.x) + 8.0f * scale;   // Round255：更靠左上角
    const float by0 = static_cast<float>(bvp.offset.y) + 8.0f * scale;
    for (int i = 0; i < 3; ++i) {
        const float cx = bx0 + kBallR + pitch * i;
        const float cy = by0 + kBallR;
        app.ballButtons[i].rect = {
            {static_cast<int32_t>(cx - kBallR), static_cast<int32_t>(cy - kBallR)},
            {static_cast<uint32_t>(2.0f * kBallR), static_cast<uint32_t>(2.0f * kBallR)}};
        app.ballButtons[i].radius = kBallR;
        if (app.ballButtons[i].color[3] == 0.0f) {   // 首次：初始化为按钮主题 normal
            for (int c = 0; c < 4; ++c) {
                app.ballButtons[i].color[c] = app.buttonTheme.normal[c];
                app.ballButtons[i].border[c] = app.buttonTheme.hoverBorder[c];
            }
        }
    }
}

// 物体显示栏文字标签纹理上传（Round252：多行列表复用；参数直接给 RGBA）。
// Round251 修复开机卡死根因：旧实现释放上一帧仍绑定的资源（未等 GPU 空闲）→ 驱动挂死。
// 现在：1) 先 vkDeviceWaitIdle 等 GPU 空闲再销毁旧纹理；2) 描述集复用不释放。
static void UploadLabelRgba(App& app, App::LabelTexture& lt,
                            const std::vector<uint8_t>& rgba, int w, int h) {
    lt.w = lt.h = 0;
    if (w <= 0 || h <= 0 || rgba.empty()) return;
    // 等 GPU 空闲：上一帧可能仍引用旧 image/view（关键修复）
    vkDeviceWaitIdle(app.device);
    if (lt.view)  vkDestroyImageView(app.device, lt.view, nullptr);
    if (lt.image) vkDestroyImage(app.device, lt.image, nullptr);
    if (lt.memory) vkFreeMemory(app.device, lt.memory, nullptr);
    lt.view = VK_NULL_HANDLE; lt.image = VK_NULL_HANDLE; lt.memory = VK_NULL_HANDLE;
    // 描述集：首次分配一次，之后复用（绝不释放，避免 in-flight 引用 UB）
    if (lt.set == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo da{};
        da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        da.descriptorPool = app.textDescriptorPool;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &app.textDescriptorLayout;
        if (vkAllocateDescriptorSets(app.device, &da, &lt.set) != VK_SUCCESS) return;
    }
    // ---- 上传纹理（staging → 设备本地图 → 视图）----
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    auto releaseStaging = [&]() {
        if (stagingBuffer) vkDestroyBuffer(app.device, stagingBuffer, nullptr);
        if (stagingMemory) vkFreeMemory(app.device, stagingMemory, nullptr);
    };
    {
        VkBufferCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = imageSize;
        b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(app.device, &b, nullptr, &stagingBuffer) != VK_SUCCESS) return;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.device, stagingBuffer, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (idx == UINT32_MAX) { releaseStaging(); return; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        if (vkAllocateMemory(app.device, &a, nullptr, &stagingMemory) != VK_SUCCESS) { releaseStaging(); return; }
        vkBindBufferMemory(app.device, stagingBuffer, stagingMemory, 0);
        void* data = nullptr;
        if (vkMapMemory(app.device, stagingMemory, 0, imageSize, 0, &data) != VK_SUCCESS) { releaseStaging(); return; }
        std::memcpy(data, rgba.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(app.device, stagingMemory);
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
    if (vkCreateImage(app.device, &img, nullptr, &lt.image) != VK_SUCCESS) { releaseStaging(); return; }
    {
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(app.device, lt.image, &mr);
        const uint32_t idx = FindMemoryType(app, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (idx == UINT32_MAX) { vkDestroyImage(app.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE; releaseStaging(); return; }
        VkMemoryAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        a.allocationSize = mr.size;
        a.memoryTypeIndex = idx;
        if (vkAllocateMemory(app.device, &a, nullptr, &lt.memory) != VK_SUCCESS) { vkDestroyImage(app.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE; releaseStaging(); return; }
        vkBindImageMemory(app.device, lt.image, lt.memory, 0);
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
    if (vkCreateImageView(app.device, &vi, nullptr, &lt.view) != VK_SUCCESS) {
        vkDestroyImage(app.device, lt.image, nullptr); lt.image = VK_NULL_HANDLE;
        vkFreeMemory(app.device, lt.memory, nullptr); lt.memory = VK_NULL_HANDLE;
        return;
    }
    // 更新描述集指向新视图（复用 lt.set，不释放）
    VkDescriptorImageInfo dii{};
    dii.sampler = app.textSampler;
    dii.imageView = lt.view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = lt.set;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &dii;
    vkUpdateDescriptorSets(app.device, 1, &wd, 0, nullptr);
    lt.w = w; lt.h = h;
}

// 多行物体名列表栅格化：每行一个名字（白字透明底），返回整块 RGBA
static bool RasterizeNameList(const std::vector<std::wstring>& names, int width, int rowH,
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

// 主循环调用：物体显示栏 = 所有物体名字的列表（Blender 风格，Round252）。
// 列表 key（全部名字 + 选中索引）变化且过节流（100ms）才重建纹理。
static void UpdateObjectLabels(App& app) {
    std::vector<std::wstring> names;
    std::wstring key;
    for (const auto& o : app.objects) {
        names.push_back(o.name);
        key += std::to_wstring(o.name.size()) + L":" + o.name + L"\n";
    }
    key += L"sel=" + std::to_wstring(app.selectedObject);
    if (key == app.objNameLabel.text) return;                 // 未变化
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - app.objLabelThrottleMs < 100) return;         // 节流
    app.objLabelThrottleMs = nowMs;
    const Layout layLbl = ComputeLayout(app);   // Round358：跟随可调右栏布局
    const int kListW = static_cast<int>(layLbl.right.extent.width) - 2 * kObjPanelPad;   // 面板内宽

    // Round346：删除最后一个（或全部）模型后 names 为空，RasterizeNameList 因 h<=0 返回 false，
    // 若不显式清空，旧列表（最后一个模型名）会残留不消失。此处直接清空标签。
    if (names.empty()) {
        app.objNameLabel.w = 0;
        app.objNameLabel.h = 0;
        app.objNameLabel.text = key;   // 记录已处理，避免下一帧反复重试
        return;
    }

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!RasterizeNameList(names, kListW, kObjPanelRowH, rgba, w, h)) return;
    UploadLabelRgba(app, app.objNameLabel, rgba, w, h);
    app.objNameLabel.text = key;
}

// ============================ 缩放距离条 + 摄像机坐标 淡入淡出 ============================
// 距离条：zoom 动作 → 淡入；静止 0.5s 淡出（Round323 精确时间规则）。
// 摄像机坐标：平移(pan)移动时满显；停止后静待 1 秒再淡出（Round349）。旋转/缩放不改变 target 故不影响。
static void UpdateNavHud(App& app) {
    static uint64_t s_lastFrameMs = 0;
    const uint64_t now = GetTickCount64();
    const float dt = s_lastFrameMs ? std::min(0.05f, static_cast<float>(now - s_lastFrameMs) / 1000.0f)
                                   : 0.016f;
    s_lastFrameMs = now;
    const float sinceMs = static_cast<float>(now - app.navLastActionMs);
    const bool active = sinceMs < 500.0f;                    // 0.5s 无动作 → 开始淡出
    const int wantZoom = (active && app.navLastActionType == 2) ? 1 : 0;
    const float inRate  = 1.0f / 0.1f;   // 出现 0.1s
    const float stopOut = 1.0f / 0.5f;   // 无动作 0.5s 淡出
    if (wantZoom) {
        app.navZoomAlpha = std::min(1.0f, app.navZoomAlpha + dt * inRate);
    } else {
        app.navZoomAlpha = std::max(0.0f, app.navZoomAlpha - dt * stopOut);
    }
    // 摄像机坐标（Round347）：检测注视点 target 位移——旋转(orbit)/缩放(zoom) 都保持 target 固定，
    // 只有平移(pan) 才改它；故旋转视角时坐标不变，仅平移时淡入。
    const Vec3& cp = app.camera.target;
    const float dpos = std::sqrt((cp.x - app.navCoordPrevPos.x) * (cp.x - app.navCoordPrevPos.x)
                               + (cp.y - app.navCoordPrevPos.y) * (cp.y - app.navCoordPrevPos.y)
                               + (cp.z - app.navCoordPrevPos.z) * (cp.z - app.navCoordPrevPos.z));
    app.navCoordPrevPos = cp;
    if (dpos > 0.001f) {
        // 平移中：满显，并刷新"最后移动时刻"
        app.navCoordAlpha = 1.0f;
        app.navCoordStopMs = now;
    } else {
        // Round351：停止平移后先静待 0.5 秒（保持满显），之后 1.0s 淡出。
        const float sinceStop = static_cast<float>(now - app.navCoordStopMs);
        if (sinceStop < 500.0f) {
            app.navCoordAlpha = 1.0f;   // 静待 0.5 秒
        } else {
            constexpr float coordFadeOut = 1.0f / 1.0f;   // Round350：淡出用 1.0s（静待 0.5s + 淡出 1s）
            app.navCoordAlpha = std::max(0.0f, app.navCoordAlpha - dt * coordFadeOut);
        }
    }
}

void DrawLogicBar(App& app, const Layout& layout) {
    const uint32_t w = app.swapchainExtent.width;
    const uint32_t h = app.swapchainExtent.height;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);

    std::array<PanelSpec, 12> panels{};
    uint32_t panelCount = 0;
    const auto addPanel = [&](VkRect2D r, VkClearColorValue fill, float radius) {
        if (r.extent.width > 0 && r.extent.height > 0) panels[panelCount++] = {r, fill, radius};
    };
    addPanel(layout.top, kPanelColor, 0.0f);
    addPanel(layout.left, kPanelColor, kCornerRadius);
    addPanel(layout.right, kPanelColor, 0.0f);   // Round346：右部模型栏无圆角
    addPanel(layout.bottom, kPanelColor, kCornerRadius);   // Round358：底部面板（默认高 150，可拖分隔线调整）
    // Round358：4 条可拖拽分隔线（1px 边框色，可视化可缩放边缘）
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

    // Round364：栏位「按下」才边缘高亮（非悬停）——按住左键且鼠标在栏位内 → 该栏位 2px 黄色亮边
    {
        const float mx = app.mouseX, my = app.mouseY;
        const auto inRect = [&](const VkRect2D& r) -> bool {
            return mx >= static_cast<float>(r.offset.x) &&
                   mx <  static_cast<float>(r.offset.x + static_cast<int32_t>(r.extent.width)) &&
                   my >= static_cast<float>(r.offset.y) &&
                   my <  static_cast<float>(r.offset.y + static_cast<int32_t>(r.extent.height));
        };
        if ((GetKeyState(VK_LBUTTON) & 0x8000) != 0) {   // 左键按住中
            VkRect2D hr{};
            float hrad = 0.0f;
            if      (inRect(layout.top))    { hr = layout.top;    hrad = 0.0f; }
            else if (inRect(layout.left))   { hr = layout.left;   hrad = kCornerRadius; }
            else if (inRect(layout.right))  { hr = layout.right;  hrad = 0.0f; }
            else if (inRect(layout.bottom)) { hr = layout.bottom; hrad = kCornerRadius; }
            if (hr.extent.width > 0 && hr.extent.height > 0) {
                const VkClearColorValue kYellow = {{1.0f, 0.84f, 0.1f, 1.0f}};
                // 面板色填充（恢复底色）+ 2px 黄色描边 → 只显示边缘亮边
                DrawPanel(app, {hr, kPanelColor, hrad, kYellow, 2.0f});
            }
        }
    }

    // Round363：顶栏按钮高度随栏位变化（上下各留 4px），图标/字符按按钮中心自动居中（图标上限 48px）
    {
        const int32_t topH = static_cast<int32_t>(layout.top.extent.height);
        if (topH > 0) {
            for (auto& b : app.buttons) {
                if (b.rect.offset.y >= static_cast<int32_t>(kTopBarHeight)) continue;   // 仅顶栏按钮
                const int32_t bh = std::max(20, topH - 8);   // 上下各留 4px；最小 20
                b.rect.offset.y = static_cast<int32_t>((topH - bh) / 2);
                b.rect.extent.height = static_cast<uint32_t>(bh);
            }
        }
    }

    for (size_t bi = 0; bi < app.buttons.size(); ++bi) {
        const UiButton& btn = app.buttons[bi];
        VkClearColorValue fill{};
        VkClearColorValue border{};
        for (int i = 0; i < 4; ++i) {
            fill.float32[i] = btn.color[i];
            border.float32[i] = btn.border[i];
        }
        // Round296：变换模式按钮（顶栏后 3 个=移动/旋转/缩放）——当前 gizmoMode 激活提亮（视觉反馈）
        if (app.buttons.size() >= 3 && bi >= app.buttons.size() - 3 &&
            static_cast<int>(bi - (app.buttons.size() - 3)) == app.gizmoMode) {
            for (int c = 0; c < 3; ++c)
                fill.float32[c] = std::min(1.0f, fill.float32[c] * 1.35f + 0.1f);
        }
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);
        DrawPanel(app, {btn.rect, fill, btn.radius, border});
        const float iconSize = std::min(static_cast<float>(btn.rect.extent.height) - 6.0f, 48.0f);   // Round363：图标上限 48px
        const float cx = static_cast<float>(btn.rect.offset.x) + btn.rect.extent.width * 0.5f;
        const float cy = static_cast<float>(btn.rect.offset.y) + btn.rect.extent.height * 0.5f;
        const VkRect2D iconRect{
            {static_cast<int32_t>(cx - iconSize * 0.5f), static_cast<int32_t>(cy - iconSize * 0.5f)},
            {static_cast<uint32_t>(iconSize), static_cast<uint32_t>(iconSize)}};
        if (app.textPipeline != VK_NULL_HANDLE) {
            // Round297：顶栏后 3 个按钮（变换模式）= 4/5/6 图标；其余按 btn.icon 选描述集
            VkDescriptorSet set;
            if (app.buttons.size() >= 3 && bi >= app.buttons.size() - 3) {
                const int ti = static_cast<int>(bi - (app.buttons.size() - 3));
                set = app.transformIcons[ti].valid ? app.transformIcons[ti].set : app.textDescriptorSet;
            } else {
                set = (btn.icon == 1) ? app.penDescriptorSet : app.textDescriptorSet;
            }
            DrawIcon(app, iconRect, kButtonIconColor, set);
        }
    }

    {
        const VkRect2D btnBar{{0, 0}, {w, h}};
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);
        // 顶栏分隔线统一样式（Round272 修正：编辑按钮右侧分割线与 x=49 灰线同长同色）
        constexpr float kTopDivY0 = 8.0f, kTopDivY1 = 28.0f;
        const VkClearColorValue kTopDivColor{{0.5f, 0.5f, 0.5f, 1.0f}};
        DrawLine(app, btnBar, 49.0f, kTopDivY0, 49.0f, kTopDivY1, kTopDivColor, 1.0f);
        if (app.editDividerX > 0 && w >= static_cast<uint32_t>(app.editDividerX) + 4) {
            DrawLine(app, btnBar, static_cast<float>(app.editDividerX), kTopDivY0,
                     static_cast<float>(app.editDividerX), kTopDivY1, kTopDivColor, 1.0f);
        }
    }

    if (g_importProgress >= 0 && g_importProgress < 100) {
        const VkRect2D& vp = layout.viewport;
        if (vp.extent.width > 0 && vp.extent.height > 0) {
            const uint64_t nowMs = GetTickCount64();
            const float bw = std::min(360.0f, static_cast<float>(vp.extent.width) * 0.5f);
            const float bh = 6.0f;
            const float bx = static_cast<float>(vp.offset.x) + (static_cast<float>(vp.extent.width) - bw) * 0.5f;
            const float by = static_cast<float>(vp.offset.y) + static_cast<float>(vp.extent.height) - 48.0f;
            const float realProg = static_cast<float>(g_importProgress.load());
            app.importDisplayProg = std::max(app.importDisplayProg, realProg);
            app.importDisplayProg += (realProg - app.importDisplayProg) * 0.2f;
            const float prog = std::clamp(app.importDisplayProg, 0.0f, 100.0f);
            VkClearColorValue bgCol = ThemeColor(ui::g_theme.slider.track, 0.85f);   // 进度条轨道底色
            VkClearColorValue fillCol = ThemeColor(ui::g_theme.slider.fill);         // 进度条填充（accent）
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
        }
    }

    // 3D 视口比例尺（用户 2026-08-20：左下角，表示当前摄像机缩放大小）
    DrawScaleBar(app, layout);

    // Round329：左键框选矩形（半透明蓝填充 + 白色细边）
    if (app.marqueeSelecting) {
        const VkRect2D& vp = layout.viewport;
        const float m0x = std::min(app.marqueeX0, app.marqueeX1);
        const float m0y = std::min(app.marqueeY0, app.marqueeY1);
        const float m1x = std::max(app.marqueeX0, app.marqueeX1);
        const float m1y = std::max(app.marqueeY0, app.marqueeY1);
        if (m1x - m0x >= 1.0f && m1y - m0y >= 1.0f) {
            const VkClearColorValue fill{{0.15f, 0.45f, 0.90f, 0.35f}};   // 半透明蓝（alpha=0.35，混合管线生效）
            const VkClearColorValue noBorder{{0, 0, 0, 0}};
            PanelSpec marq{};
            marq.rect = {{static_cast<int32_t>(m0x), static_cast<int32_t>(m0y)},
                         {static_cast<uint32_t>(m1x - m0x), static_cast<uint32_t>(m1y - m0y)}};
            marq.fill = fill;
            marq.radius = 0.0f;
            marq.border = noBorder;
            marq.borderWidth = 0.0f;
            if (app.pipelinePanelBlend != VK_NULL_HANDLE) {
                vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelinePanelBlend);
            }
            DrawPanel(app, marq);
            const VkClearColorValue bcol{{1.0f, 1.0f, 1.0f, 1.0f}};
            DrawLine(app, vp, m0x, m0y, m1x, m0y, bcol, 0.8f);
            DrawLine(app, vp, m1x, m0y, m1x, m1y, bcol, 0.8f);
            DrawLine(app, vp, m1x, m1y, m0x, m1y, bcol, 0.8f);
            DrawLine(app, vp, m0x, m1y, m0x, m0y, bcol, 0.8f);
            // 恢复默认 2D 管线（半透明混合管线仅框选填充用）
            vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        }
    }

    // 3D 视口左上角 3 个圆形按钮（Round282：与默认按钮一致——先画按钮(底+描边) 再画图标，图标不被描边遮挡）
    UpdateBallButtons(app, layout);
    {
        const VkRect2D& bvp = layout.viewport;
        if (bvp.extent.width > 0 && bvp.extent.height > 0) {
            for (int i = 0; i < 3; ++i) {
                const UiButton& b = app.ballButtons[i];
                // Round266：当前激活的渲染模式球 → 提亮 + 1.5px 黄描边
                const bool active = (i == 0 && app.renderMode == 1) || (i == 1 && app.renderMode == 0);
                // hover 状态（标准按钮动画）
                const bool hover = (b.machine.state == ButtonState::Hover);
                // 1) 按钮圆底 + 描边（先画；fill 用按钮色，描边黄/主题色；Round290 撤销彩虹环恢复统一样式）
                vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &off);
                VkClearColorValue fill{};
                for (int c = 0; c < 4; ++c) fill.float32[c] = b.color[c];
                if (active) {
                    for (int c = 0; c < 3; ++c)
                        fill.float32[c] = std::min(1.0f, fill.float32[c] * 1.4f + 0.15f);
                    const VkClearColorValue yb{{1.0f, 0.84f, 0.1f, 1.0f}};   // 黄边（选中态）
                    DrawPanel(app, {b.rect, fill, b.radius, yb, 1.5f});
                } else if (hover) {
                    const VkClearColorValue hb{{b.border[0], b.border[1], b.border[2], 1}};
                    DrawPanel(app, {b.rect, fill, b.radius, hb, 1.0f});
                } else {
                    DrawPanel(app, {b.rect, fill, b.radius, fill, 0.0f});   // 纯底无描边
                }
                // 2) 图标叠加（最后画 → 永远在最上层，不被黄边/描边遮挡）
                if (app.ballIcons[i].valid) {
                    const uint32_t isz = static_cast<uint32_t>(b.rect.extent.width) * 4u / 5u;   // 30→24px
                    const int32_t ix = b.rect.offset.x +
                                       (static_cast<int32_t>(b.rect.extent.width) - static_cast<int32_t>(isz)) / 2;
                    const int32_t iy = b.rect.offset.y +
                                       (static_cast<int32_t>(b.rect.extent.height) - static_cast<int32_t>(isz)) / 2;
                    const VkRect2D iconRect{{ix, iy}, {isz, isz}};
                    DrawIcon(app, iconRect, VkClearColorValue{{1, 1, 1, 1}}, app.ballIcons[i].set);
                }
            }
        }
    }

    // 右侧物体显示栏（Round252：多物体名称列表，Blender 风格；点击行选中）
    // Round358：跟随可调右栏布局（layout 为 ComputeLayout 单一来源）
    if (layout.right.extent.width >= 40 && layout.right.extent.height >= 100) {
        const int px = layout.right.offset.x + kObjPanelPad;
        const int py = layout.right.offset.y + kObjPanelPad;
        const int pw = static_cast<int>(layout.right.extent.width) - 2 * kObjPanelPad;
        const int ph = 250;   // Round272：物体栏高度÷2（原 500）
        const VkClearColorValue panelFill = kPanelColor;
        const VkClearColorValue whiteBorder = {{1.0f, 1.0f, 1.0f, 1.0f}};
        const VkClearColorValue selColor = ThemeColor(ui::g_theme.list.selected);
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &off);
        DrawPanel(app, {{{px - kObjPanelPad, py - kObjPanelPad},
                         {static_cast<uint32_t>(pw + 2 * kObjPanelPad), static_cast<uint32_t>(ph)}},
                        panelFill, 0.0f, whiteBorder, 1.0f});   // Round346：模型列表面板无圆角
        // Round363：物体栏行级按钮化——每行按按钮渲染（按钮主题色 normal + 悬停提亮 + 选中高亮，圆角行）
        const int nRows = static_cast<int>(app.objects.size());
        for (int i = 0; i < nRows; ++i) {
            const VkRect2D rowRect{{px, py + i * kObjPanelRowH},
                                   {static_cast<uint32_t>(pw), static_cast<uint32_t>(kObjPanelRowH)}};
            const bool hovered =
                app.mouseX >= static_cast<float>(rowRect.offset.x) &&
                app.mouseX <  static_cast<float>(rowRect.offset.x + static_cast<int32_t>(rowRect.extent.width)) &&
                app.mouseY >= static_cast<float>(rowRect.offset.y) &&
                app.mouseY <  static_cast<float>(rowRect.offset.y + static_cast<int32_t>(rowRect.extent.height));
            VkClearColorValue rc;
            for (int c = 0; c < 4; ++c) rc.float32[c] = app.buttonTheme.normal[c];
            if (i == app.selectedObject) rc = selColor;   // 选中高亮（主题选中色）
            else if (hovered) for (int c = 0; c < 3; ++c) rc.float32[c] = std::min(1.0f, app.buttonTheme.normal[c] * 1.5f + 0.08f);   // 悬停提亮
            DrawPanel(app, {rowRect, rc, 4.0f, rc, 0.0f});   // 圆角行（4px）
        }
        // 全部物体名字列表（DrawIcon 内部切换 textPipeline；透明底白字，混合显示）
        if (app.objNameLabel.set != VK_NULL_HANDLE && app.objNameLabel.w > 0) {
            const VkRect2D listRect{{px, py},
                                    {static_cast<uint32_t>(app.objNameLabel.w),
                                     static_cast<uint32_t>(app.objNameLabel.h)}};
            DrawIcon(app, listRect, VkClearColorValue{{0.88f, 0.88f, 0.88f, 1.0f}}, app.objNameLabel.set);
        }
    }

    DrawMenu(app);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void DrawFrame(App& app) {
    g_stage = "DrawFrame:交换链/acquire";
    if (app.resizePending && app.swapchain != VK_NULL_HANDLE) {
        RECT cr{};
        GetClientRect(app.hwnd, &cr);
        const uint32_t cw = static_cast<uint32_t>(cr.right - cr.left);
        const uint32_t ch = static_cast<uint32_t>(cr.bottom - cr.top);
        app.resizePending = false;
        if (cw == 0 || ch == 0) return;
        if (cw != app.swapchainExtent.width || ch != app.swapchainExtent.height) {
            if (!RecreateSwapchain(app)) return;
        }
    }
    if (app.swapchainExtent.width == 0 || app.swapchainExtent.height == 0) return;

    vkWaitForFences(app.device, 1, &app.inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = g_pfnAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX,
                                               app.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain(app);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(app.device, 1, &app.inFlightFence);

    vkResetCommandBuffer(app.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(app.commandBuffer, &beginInfo);

    VkClearValue clearValue{};
    clearValue.color = kBackgroundColor;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;

    const VkRect2D fullArea = {{0, 0}, app.swapchainExtent};
    const bool fxaa = (app.aaMode == AAMode::FXAA && app.useDynamicRendering);
    if (app.useDynamicRendering) {
        const VkImage swapImage = app.swapchainImages[imageIndex];
        TransitionBeforeRender(app, swapImage);
        ImageBarrierAspect(app, app.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        if (fxaa) {
            ImageBarrier(app, app.fxaaImage,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (fxaa) {
            colorAttachment.imageView = app.fxaaView;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else if (app.msaaEnabled) {
            colorAttachment.imageView = app.msaaColorView;
            colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            colorAttachment.resolveImageView = app.swapchainImageViews[imageIndex];
            colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else {
            colorAttachment.imageView = app.swapchainImageViews[imageIndex];
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.clearValue = clearValue;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = app.depthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue = depthClear;

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = fullArea;
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        g_pfnCmdBeginRendering(app.commandBuffer, &renderInfo);
    } else {
        VkClearValue clearValues[2] = {clearValue, depthClear};
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = app.renderPass;
        rpBegin.framebuffer = app.framebuffers[imageIndex];
        rpBegin.renderArea = fullArea;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(app.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    }
    g_stage = "DrawFrame:beginPass完成";

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(app.swapchainExtent.width),
                        static_cast<float>(app.swapchainExtent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(app.commandBuffer, 0, 1, &fullArea);

    vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);

    const Layout layout = ComputeLayout(app);

    // 3D 视口背景延伸：覆盖左右面板圆角裁切的缺口（用户 167 轮"显示黑边"）
    // —— 面板 kCornerRadius=6 把右/左边切成圆角，露出背景色（0.12，比视口 0.14 更暗）→ 视觉黑色细条
    // 把 3D 视口背景的 rect 略向左/右/上/下扩展，填上这片角缺口
    if (layout.viewport.extent.width > 0 && layout.viewport.extent.height > 0) {
        VkRect2D bgRect = layout.viewport;
        bgRect.offset.x -= static_cast<int32_t>(kCornerRadius);
        bgRect.offset.y -= static_cast<int32_t>(kCornerRadius);
        bgRect.extent.width  += 2 * static_cast<uint32_t>(kCornerRadius);
        bgRect.extent.height += 2 * static_cast<uint32_t>(kCornerRadius);
        DrawPanel(app, {bgRect, kViewportColor, 0.0f});
    }


    const float vpAspect = layout.viewport.extent.height > 0
        ? static_cast<float>(layout.viewport.extent.width) / static_cast<float>(layout.viewport.extent.height)
        : 1.0f;
    float camView[16], camProj[16], mvp[16], invVP[16];
    app.camera.ViewMatrix(camView);
    app.camera.ProjectionMatrix(vpAspect, camProj);
    MatMul4(camProj, camView, mvp);
    Mat4Inverse(mvp, invVP);

    // 渐隐中心 = **摄像机位置在 XZ 的投影**，**每帧跟随摄像机**（用户 2026-08-19 要求：
    // 旋转/平移/推拉/导入移动摄像机时，网格渐隐都要跟着动）——
    // - 用 position 而非 target：target 是注视点，与脚下网格中心存在偏移，
    //   Orbit/Zoom 后 target≠position → 渐隐圈错位；旧代码取 target.xz 正是
    //   "渐隐有偏移 + 不跟摄像机位置" BUG 的根源
    // - 导入模型 Fit 移动相机后，下一帧即自动跟随新位置（无需单独维护状态）
    app.fadeCenterXZ[0] = app.camera.position.x;
    app.fadeCenterXZ[1] = app.camera.position.z;

    VkViewport vp3d{static_cast<float>(layout.viewport.offset.x),
                    static_cast<float>(layout.viewport.offset.y),
                    static_cast<float>(layout.viewport.extent.width),
                    static_cast<float>(layout.viewport.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.commandBuffer, 0, 1, &vp3d);
    vkCmdSetScissor(app.commandBuffer, 0, 1, &layout.viewport);

    // 统一渐隐半径（Round259）：**固定默认渲染距离 = 原来的 150 × 1.7 = 255**，
    // 导入任何模型不会导致其变化（删除 Round254 按最大模型增量维护的计算代码）。
    constexpr float kDefaultFadeRadius = 150.0f * 1.7f;
    const float fadeRadius = kDefaultFadeRadius;

    // 网格地板统一在"物体之后、线框边线之前"绘制（见下方两处），保证：
    // ① 实体模式"立方体遮挡网格"；② 线框模式白线盖在网格之上（不透明）。
    if (app.vertexBuffer3D != VK_NULL_HANDLE && app.indexBuffer3D != VK_NULL_HANDLE) {
        // 颜色位深模式（用户 159 轮）→ 选对应管线（0=无颜色 1=16bit 2=8bit 3=4bit 4=1bit）
        const int cmode = app.vertexColorMode < 0 ? 0 : (app.vertexColorMode > 4 ? 4 : app.vertexColorMode);
        // Round269：实体模式 = unified3d FILL 管线组；**线框模式 = line3d + 唯一边（Blender 边框，
        // 无三角对角线），不再用 polygonMode=LINE 的三角边线**
        VkPipeline solidPipe = app.pipelineSolidNoColor;
        if (cmode == 1) solidPipe = app.pipelineSolid;
        else if (cmode == 2) solidPipe = app.pipelineSolid8;
        else if (cmode == 3) solidPipe = app.pipelineSolid4;
        else if (cmode == 4) solidPipe = app.pipelineSolid1;
        const float hasColor = (cmode == 0) ? 0.0f : (cmode == 4 ? 2.0f : 1.0f);  // 2=1bit 灰度广播
        for (const auto& obj : app.objects) {
            // 线框模式：实体几何在此跳过，统一在"网格之后"的线框 pass 绘制（保证白线盖在网格上、不透明）
            if (app.renderMode == 1) continue;
            const uint32_t n = static_cast<uint32_t>(obj.solidIndices.size());
            if (n == 0) continue;
            g_stage = "DrawFrame:物体层";
            vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solidPipe);
            VkDeviceSize vertexOffsetWire = 0;
            vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer3D, &vertexOffsetWire);
            VkDeviceSize indexOffsetWire = 0;
            vkCmdBindIndexBuffer(app.commandBuffer, app.indexBuffer3D, indexOffsetWire,
                                 VK_INDEX_TYPE_UINT32);
            float model[16] = {};
            BuildModelMatrix(obj, model);   // Round296：T·R·S（旋转/缩放）
            float mvpm[16];
            MatMul4(mvp, model, mvpm);
            Push3D push3d{};
            std::memcpy(push3d.mvp, mvpm, 64);
            push3d.mode = 1.0f;   // 实体光照（线框模式走 line3d 分支，不走这里）
            push3d.gridRadius = fadeRadius;
            push3d.modelRadius = kDefaultFadeRadius;   // Round259：固定渲染距离，不随模型变化
            push3d.hasColor = hasColor;  // 无颜色模式 shader 用常量色（修复"立方体消失"）
            // 渐隐中心 = fadeCenterXZ（摄像机位置投影，每帧跟随）
            push3d.camXZ[0] = app.fadeCenterXZ[0];
            push3d.camXZ[1] = app.fadeCenterXZ[1];
            push3d.objXZ[0] = obj.tx;
            push3d.objXZ[1] = obj.tz;
            // MC 模型（名字含"我的世界"）禁用远处渐隐 → 完全不透明
            push3d.fadeDisable =
                (obj.name.find(L"我的世界") != std::wstring::npos) ? 1.0f : 0.0f;
            vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutSolid,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(Push3D), &push3d);
            vkCmdDrawIndexed(app.commandBuffer, n, 1, obj.solidIndexOffset,
                             static_cast<int32_t>(obj.vertexOffset) +
                                 static_cast<int32_t>(obj.wireVerts.size()),
                             0);
        }
    }

    // ===== 选中物体高亮/线框预览（Round237，Blender 风格）=====
    if (app.pipelineLine3d != VK_NULL_HANDLE && app.selectedObject >= 0 &&
        app.selectedObject < static_cast<int>(app.objects.size())) {
        const SceneObject& sel = app.objects[app.selectedObject];
        if (app.renderMode != 1 && app.wireframeSel && !sel.wireIndices.empty()) {
            // Tab：线框预览——画 wireIndices（模型矩阵含平移）
            float model[16] = {};
            BuildModelMatrix(sel, model);   // Round296：T·R·S（旋转/缩放）
            float mvpm[16];
            MatMul4(mvp, model, mvpm);
            vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineLine3d);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.wireVtxBuffer3D, &off);   // Round269：专用 40B 缓冲
            vkCmdBindIndexBuffer(app.commandBuffer, app.indexBuffer3D, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, 64, mvpm);
            vkCmdDrawIndexed(app.commandBuffer, static_cast<uint32_t>(sel.wireIndices.size()), 1,
                             sel.wireIndexOffset, sel.wireVtxOffset, 0);
        } else {
            // Round359：外轮廓描边——视相关 silhouette（正面/背面分界边 + 边界边），2px 黄线
            // 替换 Round353「黄边包边」：旧实现把 importer 的唯一点 wireVerts 当顶点对非索引绘制 → 导入模型高亮乱线/不可见
            DrawSelectionOutline(app, sel, app.selectedObject, mvp);
    }
    }

    // Round329：多选高亮——其他框选物体画黄色 AABB 线框（世界坐标，直接乘 mvp）
    if (!app.multiSel.empty() && app.pipelineLine3d != VK_NULL_HANDLE) {
        constexpr int kBoxVerts = 24;
        if (EnsureHostVtxBuffer(app, app.gizmoVtxBuffer, app.gizmoVtxMem, kBoxVerts * 40)) {
            const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{3,7},{2,6}};
            for (int idx : app.multiSel) {
                if (idx == app.selectedObject || idx < 0 || idx >= static_cast<int>(app.objects.size())) continue;
                const SceneObject& mo = app.objects[idx];
                void* mapped = nullptr;
                if (vkMapMemory(app.device, app.gizmoVtxMem, 0, kBoxVerts * 40, 0, &mapped) != VK_SUCCESS) break;
                VertexSolid* v = static_cast<VertexSolid*>(mapped);
                const float minx = mo.boundsMin[0] + mo.tx, miny = mo.boundsMin[1] + mo.ty, minz = mo.boundsMin[2] + mo.tz;
                const float maxx = mo.boundsMax[0] + mo.tx, maxy = mo.boundsMax[1] + mo.ty, maxz = mo.boundsMax[2] + mo.tz;
                const float c[8][3] = {
                    {minx, miny, minz}, {maxx, miny, minz}, {minx, maxy, minz}, {maxx, maxy, minz},
                    {minx, miny, maxz}, {maxx, miny, maxz}, {minx, maxy, maxz}, {maxx, maxy, maxz},
                };
                int vi = 0;
                for (int e = 0; e < 12; ++e)
                    for (int k = 0; k < 2; ++k) {
                        VertexSolid& vv = v[vi++];
                        const int ci = edges[e][k];
                        vv.pos[0] = c[ci][0]; vv.pos[1] = c[ci][1]; vv.pos[2] = c[ci][2];
                        vv.normal[0] = 0; vv.normal[1] = 1; vv.normal[2] = 0;
                        vv.color[0] = 1.0f; vv.color[1] = 0.84f; vv.color[2] = 0.1f; vv.color[3] = 1.0f;   // 黄
                    }
                vkUnmapMemory(app.device, app.gizmoVtxMem);
                vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineLine3d);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.gizmoVtxBuffer, &off);
                vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
                vkCmdDraw(app.commandBuffer, kBoxVerts, 1, 0, 0);
            }
        }
    }

    // ===== 法线渲染（用户 166 轮，默认关闭，保留接口）=====
    // 用面法线（solidVerts.normal）画青色法线线：顶点 → 顶点 + normal×len
    if (g_showNormals && app.pipelineAxis != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:法线";
        const float normalLen = 0.3f;
        const float vw = static_cast<float>(layout.viewport.extent.width);
        const float vh = static_cast<float>(layout.viewport.extent.height);
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipelineAxis);
        for (const auto& obj : app.objects) {
            for (const auto& v : obj.solidVerts) {
                Axis3DPush p{};
                std::memcpy(p.mvp, mvp, 64);
                p.pointA[0] = v.pos[0] + obj.tx; p.pointA[1] = v.pos[1] + obj.ty; p.pointA[2] = v.pos[2] + obj.tz; p.pointA[3] = 1.0f;
                p.pointB[0] = p.pointA[0] + v.normal[0] * normalLen;
                p.pointB[1] = p.pointA[1] + v.normal[1] * normalLen;
                p.pointB[2] = p.pointA[2] + v.normal[2] * normalLen;
                p.pointB[3] = 1.0f;
                p.params[0] = 1.0f;  // 线宽 px
                p.params[1] = vw;
                p.params[2] = vh;
                p.color[0] = 0.20f; p.color[1] = 0.85f; p.color[2] = 1.00f; p.color[3] = 1.0f;  // 青蓝
                vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutAxis,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(Axis3DPush), &p);
                vkCmdDraw(app.commandBuffer, 4, 1, 0, 0);
            }
        }
    }

    // 网格地板：实体模式在物体之后（保证"立方体遮挡网格"）；线框模式在物体之后、白线之前绘制
    {
        g_stage = "DrawFrame:网格";
        app.grid.Draw(app.commandBuffer, app.vertexBuffer, mvp, invVP, app.camera, fadeRadius,
                      app.fadeCenterXZ, app.smallGridFade);
    }

    // ===== 线框模式边线（Round269/270/272 修正）：网格之后绘制 → 白线盖在网格之上、不透明 =====
    if (app.renderMode == 1 && app.vertexBuffer3D != VK_NULL_HANDLE &&
        app.indexBuffer3D != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:线框边线";
        VkPipeline wirePipe = app.pipelineLine3dWide;   // 2px 加粗，缺失回退 1px
        if (wirePipe == VK_NULL_HANDLE) wirePipe = app.pipelineLine3d;
        if (wirePipe != VK_NULL_HANDLE) {
            for (const auto& obj : app.objects) {
                // Round359：选中物体跳过硬白线框——由黄色外轮廓高亮（避免白线覆盖）
                if (app.selectedObject >= 0 &&
                    &obj == &app.objects[static_cast<size_t>(app.selectedObject)]) continue;
                const uint32_t wn = static_cast<uint32_t>(obj.wireIndices.size());
                if (wn == 0) continue;
                float model[16] = {};
                BuildModelMatrix(obj, model);   // Round296：T·R·S（旋转/缩放）
                float mvpm[16];
                MatMul4(mvp, model, mvpm);
                vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wirePipe);
                VkDeviceSize woff = 0;
                vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.wireVtxBuffer3D, &woff);
                vkCmdBindIndexBuffer(app.commandBuffer, app.indexBuffer3D, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutLine3d,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, 64, mvpm);
                vkCmdDrawIndexed(app.commandBuffer, wn, 1, obj.wireIndexOffset, obj.wireVtxOffset, 0);
            }
        }
    }

    if (app.axisVisible && app.pipelineAxis != VK_NULL_HANDLE && app.pipelineAxisOccluded != VK_NULL_HANDLE) {
        g_stage = "DrawFrame:坐标轴";
        const float axisHalfWidthPx = 2.0f;
        const float vw = static_cast<float>(layout.viewport.extent.width);
        const float vh = static_cast<float>(layout.viewport.extent.height);
        const float axisLen = 1.5f;
        const float axisCol[3][4] = {
            {0.95f, 0.30f, 0.30f, 1.0f},
            {0.30f, 0.95f, 0.40f, 1.0f},
            {0.35f, 0.55f, 1.00f, 1.0f},
        };
        const float kOccludedDim = 0.35f;
        auto drawAxis = [&](VkPipeline pipeline, float dim) {
            vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            for (int i = 0; i < 3; ++i) {
                Axis3DPush pushAxis{};
                std::memcpy(pushAxis.mvp, mvp, 64);
                pushAxis.pointA[i] = -axisLen;
                pushAxis.pointB[i] = axisLen;
                pushAxis.params[0] = axisHalfWidthPx;
                pushAxis.params[1] = vw;
                pushAxis.params[2] = vh;
                for (int c = 0; c < 3; ++c) pushAxis.color[c] = axisCol[i][c] * dim;
                pushAxis.color[3] = 1.0f;
                vkCmdPushConstants(app.commandBuffer, app.pipelineLayoutAxis,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(Axis3DPush), &pushAxis);
                vkCmdDraw(app.commandBuffer, 4, 1, 0, 0);
            }
        };
        drawAxis(app.pipelineAxisOccluded, kOccludedDim);
        drawAxis(app.pipelineAxis, 1.0f);
    }

    // ===== 变换 gizmo（Round298：移动三向标 / 旋转三色环 / 缩放轴+方块，按 gizmoMode 切换）=====
    if (app.gizmoMode == 1) DrawRotateGizmo(app, mvp, layout.viewport);
    else if (app.gizmoMode == 2) DrawScaleGizmo(app, mvp, layout.viewport);
    else DrawMoveGizmo(app, mvp, layout.viewport);

    // 3) 视口右上角坐标轴指示器（正方形视口方案——上一版，用户确认回退）——
    const VkRect2D vp = layout.viewport;
    if (vp.extent.width > 0 && vp.extent.height > 0) {
        g_stage = "DrawFrame:gizmo";
        const float gSize = kGizmoViewportSize;
        const float vpH = static_cast<float>(vp.extent.height);
        const float gcx = static_cast<float>(vp.offset.x + vp.extent.width) - kGizmoMargin;
        const float gcy = static_cast<float>(vp.offset.y) + kGizmoMargin;
        const VkRect2D gRect{{static_cast<int32_t>(gcx - gSize * 0.5f),
                              static_cast<int32_t>(gcy - gSize * 0.5f)},
                             {static_cast<uint32_t>(gSize), static_cast<uint32_t>(gSize)}};
        const float halfPx = kGizmoHalfPx * std::pow(vpH / 540.0f, 0.3f);
        const float halfSize = gSize / (2.0f * halfPx);
        float viewRot[16] = {};
        viewRot[0]  = camView[0];  viewRot[4]  = camView[4];  viewRot[8]  = camView[8];
        viewRot[1]  = camView[1];  viewRot[5]  = camView[5];  viewRot[9]  = camView[9];
        viewRot[2]  = camView[2];  viewRot[6]  = camView[6];  viewRot[10] = camView[10];
        viewRot[15] = 1.0f;
        float gOrtho[16], gizmoMvp[16];
        OrthoMatrix(halfSize, gOrtho);
        MatMul4(gOrtho, viewRot, gizmoMvp);
        VkViewport gViewport{gcx - gSize * 0.5f, gcy - gSize * 0.5f, gSize, gSize, 0.0f, 1.0f};

        vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.commandBuffer, 0, 1, &fullArea);
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);

        const float crossHalf = gSize * 0.5f - 10.0f;
        DrawLine(app, gRect, gcx - crossHalf, gcy, gcx + crossHalf, gcy,
                 kCrosshairColor, 1.0f);
        DrawLine(app, gRect, gcx, gcy - crossHalf, gcx, gcy + crossHalf,
                 kCrosshairColor, 1.0f);

        const VkClearColorValue* gizmoColors[3] = {&kAxisXColor, &kAxisYColor, &kAxisZColor};
        const Vec3 gizmoAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const Vec3 camFwd{-camView[8], -camView[9], -camView[10]};
        for (int i = 0; i < 3; ++i) {
            float ex = 0.0f, ey = 0.0f;
            if (!ProjectToViewport(gizmoMvp, gViewport, gizmoAxes[i].x, gizmoAxes[i].y, gizmoAxes[i].z, ex, ey)) continue;
            // 投影长度：轴几乎垂直屏幕（退化 <1e-3）时跳过（避免方向噪声/乱指）
            const float adx = ex - gcx;
            const float ady = ey - gcy;
            const float alen = std::sqrt(adx * adx + ady * ady);
            if (alen < 1e-3f) continue;
            const float len = std::min(alen, halfPx);
            const float ex2 = gcx + adx / alen * len;
            const float ey2 = gcy + ady / alen * len;
            const float facing = camFwd.x * gizmoAxes[i].x + camFwd.y * gizmoAxes[i].y
                               + camFwd.z * gizmoAxes[i].z;
            const float bright = std::clamp(facing * 3.0f + 0.5f, 0.0f, 1.0f);
            VkClearColorValue col = *gizmoColors[i];
            const float lum = 0.35f + 0.65f * bright;
            col.float32[0] *= lum;
            col.float32[1] *= lum;
            col.float32[2] *= lum;
            DrawLine(app, gRect, gcx, gcy, ex2, ey2, col, 1.8f);
            VkClearColorValue letterCol = *gizmoColors[i];
            letterCol.float32[0] = 0.65f + 0.35f * letterCol.float32[0];
            letterCol.float32[1] = 0.65f + 0.35f * letterCol.float32[1];
            letterCol.float32[2] = 0.65f + 0.35f * letterCol.float32[2];
            const float m = kGizmoLabelSize + 2.0f;
            const float lx = std::clamp(ex2, static_cast<float>(gRect.offset.x) + m,
                                        static_cast<float>(gRect.offset.x + gRect.extent.width) - m);
            const float ly = std::clamp(ey2, static_cast<float>(gRect.offset.y) + m,
                                        static_cast<float>(gRect.offset.y + gRect.extent.height) - m);
            DrawLetter(app, gRect, "XYZ"[i], lx, ly, kGizmoLabelSize, letterCol);
        }
    }

    // ==== FXAA 模式：3D 已渲染到中间纹理 → 结束 3D pass → FXAA 后处理 → swapchain ====
    if (fxaa) {
        g_pfnCmdEndRendering(app.commandBuffer);
        ImageBarrier(app, app.fxaaImage,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        VkRenderingAttachmentInfo colorAttachment2{};
        colorAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment2.imageView = app.swapchainImageViews[imageIndex];
        colorAttachment2.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment2.clearValue = clearValue;
        VkRenderingInfo renderInfo2{};
        renderInfo2.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo2.renderArea = fullArea;
        renderInfo2.layerCount = 1;
        renderInfo2.colorAttachmentCount = 1;
        renderInfo2.pColorAttachments = &colorAttachment2;
        g_pfnCmdBeginRendering(app.commandBuffer, &renderInfo2);

        vkCmdSetViewport(app.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(app.commandBuffer, 0, 1, &fullArea);
        vkCmdBindPipeline(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.fxaaPipeline);
        vkCmdBindVertexBuffers(app.commandBuffer, 0, 1, &app.vertexBuffer, &vertexOffset);
        vkCmdBindDescriptorSets(app.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                app.fxaaPipelineLayout, 0, 1, &app.fxaaDescriptorSet, 0, nullptr);
        const float invScreen[2] = {1.0f / static_cast<float>(app.swapchainExtent.width),
                                    1.0f / static_cast<float>(app.swapchainExtent.height)};
        vkCmdPushConstants(app.commandBuffer, app.fxaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(invScreen), invScreen);
        vkCmdDraw(app.commandBuffer, 6, 1, 0, 0);
    }

    g_stage = "DrawFrame:缩放距离条+坐标";
    UpdateNavHud(app);
    DrawCoordHud(app, layout);

    g_stage = "DrawFrame:逻辑栏/2D";
    DrawLogicBar(app, layout);

    g_stage = "DrawFrame:结束pass";
    if (app.useDynamicRendering) {
        g_pfnCmdEndRendering(app.commandBuffer);
    } else {
        vkCmdEndRenderPass(app.commandBuffer);
    }

    const VkImage swapImage = app.swapchainImages[imageIndex];
    if (app.useDynamicRendering) {
        TransitionAfterRender(app, swapImage);
    }
    vkEndCommandBuffer(app.commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &app.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &app.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &app.renderFinished;
    vkQueueSubmit(app.graphicsQueue, 1, &submitInfo, app.inFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &app.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &app.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = g_pfnQueuePresentKHR(app.graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        app.resizePending = true;
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
static void DestroyAllPipelines(App& app) {
    if (app.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipeline, nullptr); app.pipeline = VK_NULL_HANDLE; }
    if (app.pipelinePanelBlend != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelinePanelBlend, nullptr); app.pipelinePanelBlend = VK_NULL_HANDLE; }
    if (app.pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.pipelineLayout, nullptr); app.pipelineLayout = VK_NULL_HANDLE; }
    if (app.menuPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.menuPipeline, nullptr); app.menuPipeline = VK_NULL_HANDLE; }
    if (app.menuPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.menuPipelineLayout, nullptr); app.menuPipelineLayout = VK_NULL_HANDLE; }
    if (app.grid.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.grid.pipeline, nullptr); app.grid.pipeline = VK_NULL_HANDLE; }
    if (app.grid.layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.grid.layout, nullptr); app.grid.layout = VK_NULL_HANDLE; }
    if (app.pipelineSolid != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineSolid, nullptr); app.pipelineSolid = VK_NULL_HANDLE; }
    if (app.pipelineSolidNoColor != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineSolidNoColor, nullptr); app.pipelineSolidNoColor = VK_NULL_HANDLE; }
    if (app.pipelineSolid8 != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineSolid8, nullptr); app.pipelineSolid8 = VK_NULL_HANDLE; }
    if (app.pipelineSolid4 != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineSolid4, nullptr); app.pipelineSolid4 = VK_NULL_HANDLE; }
    if (app.pipelineSolid1 != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineSolid1, nullptr); app.pipelineSolid1 = VK_NULL_HANDLE; }
    if (app.pipelineLayoutSolid != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.pipelineLayoutSolid, nullptr); app.pipelineLayoutSolid = VK_NULL_HANDLE; }
    if (app.pipelineAxis != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineAxis, nullptr); app.pipelineAxis = VK_NULL_HANDLE; }
    if (app.pipelineAxisOccluded != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineAxisOccluded, nullptr); app.pipelineAxisOccluded = VK_NULL_HANDLE; }
    if (app.pipelineLayoutAxis != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.pipelineLayoutAxis, nullptr); app.pipelineLayoutAxis = VK_NULL_HANDLE; }
    if (app.pipelineLine3d != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineLine3d, nullptr); app.pipelineLine3d = VK_NULL_HANDLE; }
    if (app.pipelineLine3dWide != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineLine3dWide, nullptr); app.pipelineLine3dWide = VK_NULL_HANDLE; }
    if (app.pipelineLine3dNoDepthWide != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineLine3dNoDepthWide, nullptr); app.pipelineLine3dNoDepthWide = VK_NULL_HANDLE; }
    if (app.pipelineLine3dNoDepth != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineLine3dNoDepth, nullptr); app.pipelineLine3dNoDepth = VK_NULL_HANDLE; }
    if (app.pipelineGizmoSolid != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.pipelineGizmoSolid, nullptr); app.pipelineGizmoSolid = VK_NULL_HANDLE; }
    if (app.pipelineLayoutLine3d != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.pipelineLayoutLine3d, nullptr); app.pipelineLayoutLine3d = VK_NULL_HANDLE; }
    if (app.textPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.textPipeline, nullptr); app.textPipeline = VK_NULL_HANDLE; }
    if (app.textPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.textPipelineLayout, nullptr); app.textPipelineLayout = VK_NULL_HANDLE; }
    if (app.fxaaPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(app.device, app.fxaaPipeline, nullptr); app.fxaaPipeline = VK_NULL_HANDLE; }
    if (app.fxaaPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(app.device, app.fxaaPipelineLayout, nullptr); app.fxaaPipelineLayout = VK_NULL_HANDLE; }
}

bool ApplyAAMode(App& app, AAMode mode) {
    if (app.device == VK_NULL_HANDLE) return false;
    if (mode == app.aaMode) return true;
    vkDeviceWaitIdle(app.device);

    app.aaMode = mode;
    app.msaaEnabled = false;
    app.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    switch (mode) {
    case AAMode::MSAA_2x: app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_2_BIT; break;
    case AAMode::MSAA_4x: app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    case AAMode::SSAA:    app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    default: break;
    }

    DestroyAllPipelines(app);
    // fxaaDescriptorLayout（CreatePipelineFXAA 依赖它，必须在其之前）。
    if (!RecreateSwapchain(app)) return false;
    if (!CreatePipeline(app) || !CreateMenuPipeline(app) ||
        !CreatePipelineGrid(app) || !CreatePipelineSolid(app) ||
        !CreatePipelineAxis(app) || !CreatePipelineLine3d(app) ||
        !CreatePipelineFXAA(app) ||
        !CreateTextPipeline(app)) {
        SetError("抗锯齿管线重建失败");
        return false;
    }
    return true;
}

void Cleanup(App& app) {
    SaveSettingInt("aa_mode", static_cast<int>(app.aaMode));
    CloseSettingsWindow();
    if (app.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(app.device);
        if (app.inFlightFence != VK_NULL_HANDLE) vkDestroyFence(app.device, app.inFlightFence, nullptr);
        if (app.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(app.device, app.renderFinished, nullptr);
        if (app.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(app.device, app.imageAvailable, nullptr);
        if (app.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(app.device, app.commandPool, nullptr);
        if (app.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipeline, nullptr);
        if (app.pipelinePanelBlend != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelinePanelBlend, nullptr);
        if (app.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.pipelineLayout, nullptr);
        if (app.menuPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.menuPipeline, nullptr);
        if (app.menuPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.menuPipelineLayout, nullptr);
        if (app.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.vertexBuffer, nullptr);
        if (app.vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.vertexBufferMemory, nullptr);
        if (app.grid.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.grid.pipeline, nullptr);
        if (app.grid.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.grid.layout, nullptr);
        if (app.pipelineSolid != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineSolid, nullptr);
        if (app.pipelineSolidNoColor != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineSolidNoColor, nullptr);
        if (app.pipelineSolid8 != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineSolid8, nullptr);
        if (app.pipelineSolid4 != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineSolid4, nullptr);
        if (app.pipelineSolid1 != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineSolid1, nullptr);
        if (app.pipelineLayoutSolid != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.pipelineLayoutSolid, nullptr);
        if (app.pipelineAxis != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineAxis, nullptr);
        if (app.pipelineAxisOccluded != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineAxisOccluded, nullptr);
        if (app.pipelineLayoutAxis != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.pipelineLayoutAxis, nullptr);
        if (app.pipelineLine3d != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineLine3d, nullptr);
        if (app.pipelineLine3dWide != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineLine3dWide, nullptr);
        if (app.pipelineLine3dNoDepthWide != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineLine3dNoDepthWide, nullptr);
        if (app.pipelineLine3dNoDepth != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineLine3dNoDepth, nullptr);
        if (app.pipelineGizmoSolid != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.pipelineGizmoSolid, nullptr);
        if (app.pipelineLayoutLine3d != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.pipelineLayoutLine3d, nullptr);
        if (app.textPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.textPipeline, nullptr);
        if (app.textPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.textPipelineLayout, nullptr);
        if (app.textDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(app.device, app.textDescriptorPool, nullptr);
        if (app.textDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(app.device, app.textDescriptorLayout, nullptr);
        if (app.textSampler != VK_NULL_HANDLE) vkDestroySampler(app.device, app.textSampler, nullptr);
        if (app.textView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.textView, nullptr);
        if (app.textImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.textImage, nullptr);
        if (app.textMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.textMemory, nullptr);
        if (app.penView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.penView, nullptr);
        if (app.penImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.penImage, nullptr);
        if (app.penMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.penMemory, nullptr);
        if (app.importView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.importView, nullptr);
        if (app.importImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.importImage, nullptr);
        if (app.importMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.importMemory, nullptr);
        if (app.exportView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.exportView, nullptr);
        if (app.exportImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.exportImage, nullptr);
        if (app.exportMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.exportMemory, nullptr);
        // Round277：球按钮图标（描述集由 textDescriptorPool 统一管理，无需单独释放）
        for (int i = 0; i < 3; ++i) {
            if (app.ballIcons[i].view != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.ballIcons[i].view, nullptr);
            if (app.ballIcons[i].image != VK_NULL_HANDLE) vkDestroyImage(app.device, app.ballIcons[i].image, nullptr);
            if (app.ballIcons[i].memory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.ballIcons[i].memory, nullptr);
            app.ballIcons[i].view = VK_NULL_HANDLE; app.ballIcons[i].image = VK_NULL_HANDLE;
            app.ballIcons[i].memory = VK_NULL_HANDLE; app.ballIcons[i].set = VK_NULL_HANDLE;
            app.ballIcons[i].valid = false;
        }
        // Round297：顶栏变换按钮图标（移动/旋转/缩放）
        for (int i = 0; i < 3; ++i) {
            if (app.transformIcons[i].view != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.transformIcons[i].view, nullptr);
            if (app.transformIcons[i].image != VK_NULL_HANDLE) vkDestroyImage(app.device, app.transformIcons[i].image, nullptr);
            if (app.transformIcons[i].memory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.transformIcons[i].memory, nullptr);
            app.transformIcons[i].view = VK_NULL_HANDLE; app.transformIcons[i].image = VK_NULL_HANDLE;
            app.transformIcons[i].memory = VK_NULL_HANDLE; app.transformIcons[i].set = VK_NULL_HANDLE;
            app.transformIcons[i].valid = false;
        }
        if (app.vertexBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.vertexBuffer3D, nullptr);
        if (app.vertexBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.device, app.vertexBufferMemory3D, nullptr);
        if (app.indexBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.indexBuffer3D, nullptr);
        if (app.indexBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.device, app.indexBufferMemory3D, nullptr);
        if (app.wireVtxBuffer3D != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.wireVtxBuffer3D, nullptr);
        if (app.wireVtxBufferMemory3D != VK_NULL_HANDLE) vkFreeMemory(app.device, app.wireVtxBufferMemory3D, nullptr);
        if (app.selVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.selVtxBuffer, nullptr);
        if (app.selVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.device, app.selVtxMem, nullptr);
        if (app.gizmoVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.gizmoVtxBuffer, nullptr);
        if (app.gizmoVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.device, app.gizmoVtxMem, nullptr);
        if (app.gizmoSolidVtxBuffer != VK_NULL_HANDLE) vkDestroyBuffer(app.device, app.gizmoSolidVtxBuffer, nullptr);
        if (app.gizmoSolidVtxMem != VK_NULL_HANDLE) vkFreeMemory(app.device, app.gizmoSolidVtxMem, nullptr);
        // 物体显示栏标签纹理（Round249）+ 距离比例尺（Round310）
        for (App::LabelTexture* lt : {&app.objNameLabel, &app.scaleLabel,
                                       &app.coordLabels[0], &app.coordLabels[1],
                                       &app.coordLabels[2]}) {
            if (lt->view) vkDestroyImageView(app.device, lt->view, nullptr);
            if (lt->image) vkDestroyImage(app.device, lt->image, nullptr);
            if (lt->memory) vkFreeMemory(app.device, lt->memory, nullptr);
            lt->view = VK_NULL_HANDLE; lt->image = VK_NULL_HANDLE; lt->memory = VK_NULL_HANDLE;
        }
        if (app.depthView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.depthView, nullptr);
        if (app.depthImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.depthImage, nullptr);
        if (app.depthMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.depthMemory, nullptr);
        if (app.msaaColorView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.msaaColorView, nullptr);
        if (app.msaaColorImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.msaaColorImage, nullptr);
        if (app.msaaColorMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.msaaColorMemory, nullptr);
        if (app.fxaaPipeline != VK_NULL_HANDLE) vkDestroyPipeline(app.device, app.fxaaPipeline, nullptr);
        if (app.fxaaPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(app.device, app.fxaaPipelineLayout, nullptr);
        if (app.fxaaDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(app.device, app.fxaaDescriptorPool, nullptr);
        if (app.fxaaDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(app.device, app.fxaaDescriptorLayout, nullptr);
        if (app.fxaaSampler != VK_NULL_HANDLE) vkDestroySampler(app.device, app.fxaaSampler, nullptr);
        if (app.fxaaView != VK_NULL_HANDLE) vkDestroyImageView(app.device, app.fxaaView, nullptr);
        if (app.fxaaImage != VK_NULL_HANDLE) vkDestroyImage(app.device, app.fxaaImage, nullptr);
        if (app.fxaaMemory != VK_NULL_HANDLE) vkFreeMemory(app.device, app.fxaaMemory, nullptr);
        for (VkFramebuffer fb : app.framebuffers) vkDestroyFramebuffer(app.device, fb, nullptr);
        if (app.renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(app.device, app.renderPass, nullptr);
        for (VkImageView view : app.swapchainImageViews) vkDestroyImageView(app.device, view, nullptr);
        if (app.swapchain != VK_NULL_HANDLE) g_pfnDestroySwapchainKHR(app.device, app.swapchain, nullptr);
        vkDestroyDevice(app.device, nullptr);
    }
#ifdef VKB_ENABLE_VALIDATION
    if (g_debugMessenger != VK_NULL_HANDLE && g_pfnDestroyDebugUtilsMessengerEXT) {
        g_pfnDestroyDebugUtilsMessengerEXT(app.instance, g_debugMessenger, nullptr);
    }
#endif
    if (app.surface != VK_NULL_HANDLE) {
        auto pfnDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            vkGetInstanceProcAddr(app.instance, "vkDestroySurfaceKHR"));
        if (pfnDestroySurfaceKHR) pfnDestroySurfaceKHR(app.instance, app.surface, nullptr);
    }
    if (app.instance != VK_NULL_HANDLE) vkDestroyInstance(app.instance, nullptr);
    if (app.hwnd != nullptr) DestroyWindow(app.hwnd);
}

// 依赖 DLL 检测（用户要求）：软件依赖的 .dll 从当前根目录（exe 所在目录）加载，
// 缺失时明确报错退出，而不是静默失败或依赖系统目录兜底
// 获取真实 Windows 版本（用 RtlGetVersion 动态加载，避免 GetVersionEx 在
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
// 进程崩溃而非优雅失败——此兜底保证下次崩溃一定有日志和弹窗信息）
// ---------------------------------------------------------------------------
// 进程崩溃而非优雅失败——此兜底保证下次崩溃一定有日志和弹窗信息）
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

// 鼠标灵敏度（0~100）→ orbit/pan 输入增益因子（0.1~2.0，50→1.05 接近中性）
static float SensToFactor(int s) {
    const float c = std::max(0, std::min(100, s)) / 100.0f;
    return 0.1f + c * 1.9f;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Round291：DPI 感知——Windows 缩放下 swapchain 用物理像素，2D 不再被系统放大模糊
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFn)(HANDLE);
        const auto dpiCtx = hUser32 ? reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext")) : nullptr;
        if (dpiCtx) {
            dpiCtx(reinterpret_cast<HANDLE>(-4));   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (Win10 1803+)
        } else {
            SetProcessDPIAware();   // Win7/Win8 回退（系统级 DPI 感知）
        }
    }

    // Round275：单实例检查——已有相同程序运行时弹窗退出，
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
        // 与 -fshort-wchar 的 2 字节 UTF-16 错位 → 必须自己按 wchar_t 步进遍历）
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

    // 抗锯齿默认：核显（集成 GPU）默认**无抗锯齿**（性能优先，用户 165 轮）；
    // 独立显卡默认 FXAA。保存过设置/环境变量时以用户选择为准
    app.aaMode = (app.gpuType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? AAMode::None : AAMode::FXAA;
    const int savedAa = LoadSettingInt("aa_mode", -1);
    if (savedAa >= 0 && savedAa <= 4) {
        app.aaMode = static_cast<AAMode>(savedAa);
    } else if (const char* env = std::getenv("AWA_AA"); env && env[0] >= '0' && env[0] <= '4' && env[1] == 0) {
        app.aaMode = static_cast<AAMode>(env[0] - '0');
    }
    app.msaaEnabled = false;
    app.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    switch (app.aaMode) {
    case AAMode::MSAA_2x: app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_2_BIT; break;
    case AAMode::MSAA_4x: app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    case AAMode::SSAA:    app.msaaEnabled = true; app.msaaSamples = VK_SAMPLE_COUNT_4_BIT; break;
    default: break;
    }
    g_selectedAaMode = static_cast<int>(app.aaMode);

    // 摄像机阻尼（用户 168 轮设置项）：从 awa_settings.txt 加载（0~100，默认 85）
    const int savedDamp = LoadSettingInt("camera_damping", -1);
    if (savedDamp >= 0 && savedDamp <= 100) {
        app.camera.damping = savedDamp / 100.0f;
        g_cameraDamping = savedDamp;
    } else {
        g_cameraDamping = static_cast<int>(app.camera.damping * 100.0f + 0.5f);
    }

    // 鼠标滑动灵敏度（用户 2026-08-20 设置项）：从 awa_settings.txt 加载（0~100，默认 50）
    const int savedSens = LoadSettingInt("mouse_sensitivity", -1);
    if (savedSens >= 0 && savedSens <= 100) {
        g_mouseSensitivity = savedSens;
        app.camera.orbitSensitivity = SensToFactor(savedSens);
    } else {
        g_mouseSensitivity = 50;
        app.camera.orbitSensitivity = SensToFactor(50);
    }

    // Round364：面板尺寸持久化——顶栏固定默认高度（不读 panel_top_h，取消顶栏缩放）；左/右/底可拖
    app.panelTopH = kTopBarHeight;
    {
        const int l = LoadSettingInt("panel_left_w", -1);
        if (l >= static_cast<int>(kSideBarWidth))    app.panelLeftW = static_cast<uint32_t>(l);
        const int rr = LoadSettingInt("panel_right_w", -1);
        if (rr >= static_cast<int>(kSideBarWidth))   app.panelRightW = static_cast<uint32_t>(rr);
        const int b = LoadSettingInt("panel_bottom_h", -1);
        if (b >= static_cast<int>(kBottomBarHeight)) app.panelBottomH = static_cast<uint32_t>(b);
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
    if (!app.useDynamicRendering && (!CreateRenderPass(app) || !CreateFramebuffers(app))) {
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
    initStep(CreateMenuPipeline(app), "CreateMenuPipeline");
    initStep(CreatePipelineGrid(app), "CreatePipelineGrid");
    initStep(CreatePipelineSolid(app), "CreatePipelineSolid");
    initStep(CreateVertexBuffer3D(app), "CreateVertexBuffer3D");
    initStep(CreatePipelineAxis(app), "CreatePipelineAxis");
    initStep(CreatePipelineLine3d(app), "CreatePipelineLine3d");
    initStep(CreatePipelineFXAA(app), "CreatePipelineFXAA");
    initStep(CreateCommandResources(app), "CreateCommandResources");
    if (!initOk) {
        ShowErrorBox(g_error.c_str());
        Cleanup(app);
        return 1;
    }

    // 注：图标纹理创建失败**降级不退出**（核显/老驱动 WIC 或纹理创建异常时保证软件能打开，
    //     仅图标缺失；DrawIcon 对 NULL pipeline/set 有保护）
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
                               app.penImage, app.penMemory, app.penView, app.penDescriptorSet)) {
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
                               app.importImage, app.importMemory, app.importView, app.importDescriptorSet)) {
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
                               app.exportImage, app.exportMemory, app.exportView, app.exportDescriptorSet)) {
            VkbLog(("[icon] 导出图标纹理创建失败（降级：无图标）: " + g_error).c_str());
            g_error.clear();
        }
    }

    {
        g_stage = "球按钮图标(LoadBallIcons)";
        LoadBallIcons(app);
    }

    g_stage = "按钮初始化:LoadButtonTheme";
    app.buttonTheme = LoadButtonTheme();
    {
        UiButton b;
        b.rect = {{8, 4}, {28, 28}};
        b.radius = 4.0f;
        b.icon = 0;
        b.onClick = [](App& a) {
            a.menuOpen = false;
            const int gpuMaj = VK_API_VERSION_MAJOR(a.gpuApiVersion);
            const int gpuMin = VK_API_VERSION_MINOR(a.gpuApiVersion);
            const wchar_t* typeW = L"未知设备";
            switch (a.gpuType) {
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeW = L"核显"; break;
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeW = L"独立显卡"; break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeW = L"虚拟 GPU"; break;
                case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeW = L"CPU 软件渲染"; break;
                default: break;
            }
            // 厂商识别（左下角仅显示短码：Intel / AMD / NV / 其他）
            const wchar_t* vendorW = L"GPU";
            switch (a.gpuVendor) {
                case 0x8086: vendorW = L"Intel"; break;
                case 0x1002: case 0x1022: vendorW = L"AMD"; break;
                case 0x10DE: vendorW = L"NV"; break;
                default: break;
            }
            wsprintfW(g_renderBottomText,
                      L"awa %s · 著作人：%s · vulkan %d.%d - %s渲染 · %s",
                      kVersionW, kAuthorW, gpuMaj, gpuMin, typeW, vendorW);
            OpenSettingsWindow(a.hwnd, static_cast<int>(a.aaMode));
        };
        for (int i = 0; i < 4; ++i) {
            b.color[i] = app.buttonTheme.normal[i];
            b.border[i] = kBorderColor.float32[i];
        }
        app.buttons.push_back(b);
    }
    {
        UiButton b;
        b.rect = {{62, 4}, {50, 28}};
        b.radius = 4.0f;
        b.icon = 1;
        b.onClick = [](App& a) {
            a.menuOpen = !a.menuOpen;
            a.menuAnimStartMs = GetTickCount64();
            a.menuAnimFrom = a.menuAnim;
        };
        for (int i = 0; i < 4; ++i) {
            b.color[i] = app.buttonTheme.normal[i];
            b.border[i] = kBorderColor.float32[i];
        }
        app.buttons.push_back(b);
    }

    if (!app.buttons.empty()) {
        const UiButton& editBtn = app.buttons.back();
        const int btnCenterX = editBtn.rect.offset.x + static_cast<int32_t>(editBtn.rect.extent.width) / 2;
        const int menuX = std::max(btnCenterX - 100, 0);
        app.menuRect = {
            {menuX,
             editBtn.rect.offset.y + static_cast<int32_t>(editBtn.rect.extent.height) + 2},
            {200, 0}
        };
        // Round272：编辑按钮右侧竖直分割线 x 坐标（右边缘 + 150，Round274 左移 80px → +70）
        app.editDividerX = editBtn.rect.offset.x + static_cast<int32_t>(editBtn.rect.extent.width) + 70;
    }

    // Round288：第二条分割线右侧新建 3 个相连按钮（各宽 50px、无间隔）
    // Round296：绑定变换模式——按钮1=移动 按钮2=旋转 按钮3=缩放（gizmoMode 0/1/2）
    if (app.editDividerX > 0) {
        for (int i = 0; i < 3; ++i) {
            UiButton b;
            b.rect = {{app.editDividerX + 12 + i * 50, 4}, {50, 28}};
            b.radius = 4.0f;
            b.icon = 0;
            b.onClick = nullptr;   // Round296：变换模式在 WM_LBUTTONUP 按按钮索引设置（onClick 是函数指针不能捕获）
            for (int c = 0; c < 4; ++c) {
                b.color[c] = app.buttonTheme.normal[c];
                b.border[c] = kBorderColor.float32[c];
            }
            app.buttons.push_back(b);
        }
    }

    {
        constexpr float kMenuPad = 10.0f, kMenuItemH = 30.0f, kItemGap = 5.0f;
        const float mx = static_cast<float>(app.menuRect.offset.x);
        const float my = static_cast<float>(app.menuRect.offset.y);
        const float mw = static_cast<float>(app.menuRect.extent.width);
        const float itemY = my + kMenuPad;
        const float itemW = (mw - 2.0f * kMenuPad - kItemGap) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const float x = mx + kMenuPad + static_cast<float>(i) * (itemW + kItemGap);
            app.menuItems[i].rect = {
                {static_cast<int32_t>(x), static_cast<int32_t>(itemY)},
                {static_cast<uint32_t>(itemW), static_cast<uint32_t>(kMenuItemH)}};
            app.menuItems[i].radius = 2.0f;
            for (int j = 0; j < 4; ++j) {
                app.menuItems[i].color[j] = app.buttonTheme.normal[j];
                app.menuItems[i].border[j] = kBorderColor.float32[j];
            }
        }
        app.menuItems[0].onClick = [](App& a) {
            a.menuOpen = false;
            // 用户 180 轮：点击导入 → 先弹出 500×700 默认色导入窗口（内容选择），
            // 窗口内"导入 3D 模型…"确认后经 g_importWindowConfirm 走原文件选择流程
            OpenImportWindow(a.hwnd);
        };
    }

    ShowWindow(app.hwnd, SW_MAXIMIZE);
    g_stage = "主循环";

    MSG msg{};
    while (app.running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) app.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (app.running) {
            if (g_selectedAaMode != static_cast<int>(app.aaMode)) {
                ApplyAAMode(app, static_cast<AAMode>(g_selectedAaMode));
            }
            // 摄像机阻尼实时应用（用户 168 轮）：设置窗口滑动条改 g_cameraDamping，
            // 主循环检测差异 → 应用到 camera.damping（下一帧 UpdateSmooth 生效）+ 保存
            {
                const int dampNow = static_cast<int>(app.camera.damping * 100.0f + 0.5f);
                if (g_cameraDamping != dampNow) {
                    app.camera.damping = g_cameraDamping / 100.0f;
                    SaveSettingInt("camera_damping", g_cameraDamping);
                }
            }
            // 鼠标滑动灵敏度实时应用（用户 2026-08-20）：设置窗口滑动条改 g_mouseSensitivity，
            // 主循环检测差异 → 映射到 camera.orbitSensitivity（Orbit/Pan 输入增益）+ 保存
            {
                const float sensFactor = SensToFactor(g_mouseSensitivity);
                if (std::fabs(sensFactor - app.camera.orbitSensitivity) > 1e-3f) {
                    app.camera.orbitSensitivity = sensFactor;
                    SaveSettingInt("mouse_sensitivity", g_mouseSensitivity);
                }
            }
            app.camera.UpdateSmooth();
            for (auto& b : app.buttons) UpdateButtonColor(b, app.buttonTheme);
            for (auto& b : app.ballButtons) UpdateButtonColor(b, app.buttonTheme);
            for (auto& b : app.menuItems) UpdateButtonColor(b, app.buttonTheme);
            {
                const float t = static_cast<float>(GetTickCount64() - app.menuAnimStartMs) / 1000.0f;
                if (app.menuOpen) {
                    const float k = std::min(t / 0.0375f, 1.0f);
                    app.menuAnim = app.menuAnimFrom + (1.0f - app.menuAnimFrom) * k;
                } else {
                    if (t < 0.0125f) {
                        app.menuAnim = app.menuAnimFrom;
                    } else {
                        const float k = std::min((t - 0.0125f) / 0.025f, 1.0f);
                        app.menuAnim = app.menuAnimFrom * (1.0f - k);
                    }
                }
            }
            // 菜单完全关闭后：重置菜单项按钮状态（避免重新打开时残留 Hover/Pressed 高亮）
            if (!app.menuOpen && app.menuAnim < 0.01f) {
                for (int i = 0; i < 2; ++i) app.menuItems[i].machine.state = ButtonState::Normal;
            }
            // 导入窗口确认"导入 3D 模型…"（用户 181 轮：路径已选好，直接启动导入）
            if (g_importWindowConfirm) {
                g_importWindowConfirm = false;
                if (!g_importPath.empty()) {
                    LaunchImport(app, g_importPath.c_str());
                    g_importPath.clear();
                }
            }
            // 导入窗口确认"地图导入(be)"：开窗(loading进度条，0%起)→后台线程验证/加载（Round207：异步，不卡UI）
            if (g_mcWorldConfirm) {
                g_mcWorldConfirm = false;
                if (!g_mcWorldPath.empty()) {
                    McWorldImporter::Instance().BeginLoad(app.hwnd, g_mcWorldPath);
                    g_mcWorldPath.clear();
                }
            }
            // 避免 GetOpenFileNameW 阻塞消息循环导致按钮动画卡在按下状态
            if (app.pendingImport &&
                GetTickCount64() - app.pendingImportAtMs >= 200) {
                app.pendingImport = false;
                wchar_t path[MAX_PATH];
                if (PickModelFile(app.hwnd, path, MAX_PATH)) {
                    LaunchImport(app, path);
                }
            }
            // 物体显示栏标签（Round249：右侧面板展示当前物体名称/位置）
            UpdateObjectLabels(app);
            DrawFrame(app);
        }
    }

    // 主循环退出前先等后台导入线程，避免后台线程访问已销毁对象
    WaitForImportThread();
    vkDeviceWaitIdle(app.device);
    Cleanup(app);
    return 0;
}

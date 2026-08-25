// ============================================================================
//  awa_internal.h（#208 拆分产出）：main.cpp 拆分后跨文件共享的结构/常量/声明
//  各拆分文件（vulkan_init/renderer/picking/gizmo/ui3d）与 main.cpp 共同包含
// ============================================================================
#pragma once

#include <windows.h>
#include <commdlg.h>
#include "app.h"  // App/SceneObject/VertexSolid/AAMode；并传递包含 vulkan_loader/camera/model_import/settings_window/ui_presets
            // （ui_button.h / import_pipeline.h 不再经此间接引入——各自 .cpp 显式 include，避免上帝头）
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

using std::uint32_t;

// 鼠标灵敏度（0~100）↔ orbit/pan 输入增益因子（0.1~2.0，50→1.05 接近中性）
inline float SensToFactor(int s) {
    const float c = std::max(0, std::min(100, s)) / 100.0f;
    return 0.1f + c * 1.9f;
}
inline int FactorToSens(float f) {
    int s = static_cast<int>((f - 0.1f) / 1.9f * 100.0f + 0.5f);
    return std::max(0, std::min(100, s));
}

// ============ 共享结构（原 main.cpp 文件级定义）============
// ---- 界面布局 ----
// kTopBarHeight / kSideBarWidth / kBottomBarHeight 已移至 app.h（App 默认值 + 拖拽最小限制共用）
constexpr float kLineWidth = 1.0f;
// 右侧物体列表Blender 风格多物体编辑栏行高 / 面板内边距（绘制与点击命中共用）
constexpr int kObjPanelRowH = 22;
constexpr int kObjPanelPad  = 8;
constexpr int kObjTitleH    = 24;   // 物体栏顶部标题条高（卡片列表控件，方案B）
constexpr int kObjRowGap    = 0;    // 物体栏行间距（Blender Outliner 紧凑风格：无间距）
constexpr int kObjMaxRows   = 5;    // 物体栏固定显示栏数（用户要求固定高度约 5 栏）

// 从 ui::g_theme 预设 COLORREF 转 Vulkan clear color（仅 2D UI 用；3D 视口/轴/gizmo 保持原样）
inline VkClearColorValue ThemeColor(COLORREF c, float a = 1.0f) {
    const ui::FloatColor f = ui::ToFloat(c, a);
    VkClearColorValue v{};
    v.float32[0] = f.r; v.float32[1] = f.g; v.float32[2] = f.b; v.float32[3] = f.a;
    return v;
}

// ---- 颜色方案（2D UI 从 ui::g_theme 预设取值；3D 视口/轴/gizmo 颜色保持原样，勿动）----
constexpr VkClearColorValue kBackgroundColor = {{0.12f, 0.12f, 0.12f, 1.0f}}; // 3D 视口背景
const VkClearColorValue kPanelColor = ThemeColor(ui::g_theme.palette.bg); // 顶栏/侧栏/面板背景
constexpr VkClearColorValue kViewportColor = {{0.14f, 0.14f, 0.14f, 1.0f}}; // 3D 视口延伸背景
const VkClearColorValue kBorderColor = ThemeColor(ui::g_theme.palette.panelBorder); // 面板/按钮描边
const VkClearColorValue kButtonIconColor = ThemeColor(ui::g_theme.palette.text); // 图标/按钮文字

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
struct Push3D {
    float mvp[16];
    float mode;
    float gridRadius;
    float modelRadius;
    float camXZ[2];
    float objXZ[2];
 float hasColor; // 0=无颜色（shader 用常量色），1=有颜色（用 vColor）
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
struct TextPush {
    float rect[4];
    float color[4];
    float mono;   // 0=直通彩色（软件图标特殊通道）；1=纯白渲染（按钮图标默认）
};
static_assert(sizeof(TextPush) == 36, "TextPush 布局需与 text shader 一致");
// ---------------------------------------------------------------------------
constexpr float kDefaultFadeRadius = 150.0f * 1.7f; // 固定默认渲染距离150 x 1.7 = 255



// ---- 布局/面板结构（原 main.cpp 文件级定义）----
struct Layout {
    VkRect2D top;
    VkRect2D left;
    VkRect2D right;
 VkRect2D bottom; // 底部面板（默认高 150）
    VkRect2D viewport;
};

struct PanelSpec {
    VkRect2D rect;
    VkClearColorValue fill;
    float radius;
    VkClearColorValue border = kBorderColor;
 float borderWidth = kLineWidth; // 可关闭描边（0=不画，去白边）
};

// ============ 全局状态（定义在 main.cpp / import_pipeline.cpp，跨文件共享）============

// ---- 应用标识/全局开关（定义于 main.cpp，跨文件共享）----
constexpr std::string_view kAppName = "awa";
extern bool g_useQuads;
extern bool g_showNormals;
extern bool g_swapZtoY;
extern wchar_t g_startupObjPath[MAX_PATH];

extern std::string g_error;
extern const char* g_stage;
extern void SetError(const std::string& msg);
extern void ShowErrorBox(const char* utf8Msg);

// ============ 跨文件函数声明（#208 拆分后）============

// ---- vulkan_init.cpp ----
VkCommandBuffer BeginOneTimeCommand(const App& app);
bool ApplyAAMode(App& app, AAMode mode);
bool CreateCommandResources(App& app);
bool CreateDepthResources(App& app);
bool CreateDevice(App& app);
bool CreateFXAAResources(App& app);
bool CreateFramebuffers(App& app);
bool CreateIconTexture(App& app, const std::vector<uint8_t>& rgba, int w, int h, VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView, VkDescriptorSet& outSet);
bool CreateInstance(App& app);
bool CreateMSAAColorResources(App& app);
bool CreateMenuPipeline(App& app);
bool CreateOutlineResources(App& app);
bool CreatePanelBlendPipeline(App& app, VkPipelineLayout layout, VkPipeline& outPipeline, VkSampleCountFlagBits samples);
bool CreatePipeline(App& app);
bool CreatePipelineAxis(App& app);
bool CreatePipelineUI(App& app);
bool CreatePipelineFXAA(App& app);
bool CreatePipelineGrid(App& app);
bool CreatePipelineLine3d(App& app);
bool CreatePipelineOutline(App& app);
bool CreatePipelineSolid(App& app);
bool CreateRenderPass(App& app);
bool CreateRoundedRectPipeline(App& app, VkPipelineLayout& outLayout, VkPipeline& outPipeline, VkSampleCountFlagBits samples);
bool CreateShaderModule(VkDevice device, const unsigned char* code, size_t size, VkShaderModule& module);
bool CreateSurface(App& app, HINSTANCE hInstance);
bool CreateSwapchain(App& app, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
bool CreateTextPipeline(App& app);
bool CreateTextResources(App& app, const std::vector<uint8_t>& rgba, int w, int h);
bool CreateVertexBuffer(App& app);
bool CreateVertexBuffer3D(App& app);
bool LoadBallIcons(App& app);
bool PickPhysicalDevice(App& app);
bool RecreateSwapchain(App& app);
uint16_t FloatToHalf(float f);
uint32_t FindMemoryType(const App& app, uint32_t typeFilter, VkMemoryPropertyFlags props);
void Cleanup(App& app);
void CmdImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess);
void DestroyAllPipelines(App& app);
void EndOneTimeCommand(const App& app, VkCommandBuffer cmd);
// 图像布局屏障（app 命令缓冲区版）。aspect 默认 COLOR；深度等用途显式传 VK_IMAGE_ASPECT_DEPTH_BIT。
void ImageBarrier(App& app, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

// ---- renderer.cpp ----
void BeginFrameRendering(App& app, uint32_t imageIndex, bool fxaa, const VkClearValue& clearValue, const VkClearValue& depthClear, const VkRect2D& fullArea);
void DrawCoordHud(App& app, const Layout& layout);
void DrawFrame(App& app);
void DrawGridFloor(App& app, const float* mvp, const float* invVP, float fadeRadius);
void DrawSelectionMaskPass(App& app, const float* mvp, const VkRect2D& fullArea, const Layout& layout, const VkViewport& viewport, bool selValid);
void DrawSelectionOutline(App& app, SceneObject& sel, int selIndex, const float mvp[16]);
void DrawViewportGizmoIndicator(App& app, const float* camView, const Layout& layout, const VkViewport& viewport, const VkRect2D& fullArea, VkDeviceSize vertexOffset);
void EndFrameAndPresent(App& app, uint32_t imageIndex);

// ---- picking.cpp ----
bool BuildViewRay(App& app, float mx, float my, float o[3], float d[3]);
bool ClosestAxisParam(const float a[3], const float dir[3], const float o[3], const float d[3], float& t);
bool HitGizmoRingAt(App& app, float mx, float my);
bool RayPlane(const float o[3], const float d[3], const float n[3], const float p[3], float out[3]);
bool RayTriangle(const float* o, const float* d, const float* a, const float* b, const float* c, float& t);
float DistPointToSeg2D(float px, float py, float ax, float ay, float bx, float by);
float GizmoAxisLen(App& app, const float pivot[3], const VkRect2D& vp);
float RayAABB(const float* o, const float* d, float minx, float miny, float minz, float maxx, float maxy, float maxz);
int PickGizmoAxisAt(App& app, float mx, float my);
int PickObjectAt(App& app, float mx, float my);
int PickRotateGizmoAt(App& app, float mx, float my);
int PickScaleGizmoAt(App& app, float mx, float my);
void BuildFeatureVerts(SceneObject& o);
void BuildObjectWireframe(SceneObject& o);
void BuildSelSilhouette(const SceneObject& o, App& app);
void GizmoPivot(const SceneObject& o, float out[3]);
void GizmoPivotSelected(App& app, float out[3]);   // 多选（框选）时取各物体中心中点
void StartFreeDrag(App& app, HWND hwnd, float mx, float my);

// ---- gizmo.cpp ----
bool EnsureHostVtxBuffer(App& app, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size);
bool Mat4Inverse(const float m[16], float out[16]);
bool ProjectToViewport(const float mvp[16], const VkViewport& viewport, float px, float py, float pz, float& sx, float& sy);
void BuildModelMatrix(const SceneObject& o, float out[16]);
void BuildRotFromEuler(float rx, float ry, float rz, float R[16]);
void DrawMoveGizmo(App& app, const float mvp[16], const VkRect2D& vp);
void DrawRotateGizmo(App& app, const float mvp[16], const VkRect2D& vp);
void DrawScaleGizmo(App& app, const float mvp[16], const VkRect2D& vp);
void EulerFromR(const float R[16], float& rx, float& ry, float& rz);
void GizmoFillCone(const float* tip, const float* base, float rad, const float* u, const float* w, VertexSolid* v, int& vi, int n, const float* col);  // 创建并绑定 HOST 可见顶点缓冲惰性：首次创建复用；容量不足时销毁重建， // 因旋转/缩放 gizmo 与移动三向标共用缓冲且顶点数不同 static bool EnsureHostVtxBuffer(App& app, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size);
void GizmoFillCube(const float* c, float hs, VertexSolid* v, int& vi, const float* col);
void GizmoFillSphere(const float* c, float rad, VertexSolid* v, int& vi, int sx, int sy, const float* col);
void GizmoFillVert(VertexSolid& vv, const float* pos, const float* col);
void GizmoPerpBasis(const float d[3], float u[3], float w[3]);
void MakeWorldRot(int axis, float deg, float R[16]);
void MatMul4(const float a[16], const float b[16], float out[16]);
void OrthoMatrix(float halfSize, float out[16]);
void RotateSelectedObject(App& app, char axis, float deg);
void ScaleSelectedObject(App& app, float factor);

// ---- ui3d.cpp ----
App* GetApp(HWND hwnd);
ButtonTheme LoadButtonTheme();
LRESULT CALLBACK RenameEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
Layout ComputeLayout(const App& app);
// 顶栏控件布局（每帧在 DrawLogicBar 中调用）：设置/文件/编辑 宽按钮 + 移动/旋转/缩放 垂直排布左部。
// 统一按钮/分隔线/菜单位置。
void ComputeTopBar(App& app, const Layout& layout);
// 读取 exe 同目录 button_labels.txt 的 label0/label1/label2（UTF-8，外部可改按钮文字），未配置保留默认值
void LoadButtonLabels(UiButton* buttons, int n);
bool DecodePngWic(const unsigned char* pngData, size_t pngSize, std::vector<uint8_t>& rgba, int& width, int& height, int targetW = 0, int targetH = 0);
bool RasterizeNameList(const std::vector<std::wstring>& names, int width, int rowH, std::vector<uint8_t>& rgba, int& outW, int& outH);
bool RasterizeText(const wchar_t* text, int fontSize, int pad, const wchar_t* fontName, std::vector<uint8_t>& rgba, int& width, int& height);
float MouseX(LPARAM lParam);
float MouseY(LPARAM lParam);
int HitResizeDivider(const App& app, float mx, float my);
int LoadSettingInt(const char* key, int defaultValue);
std::string TrimStr(const std::string& s);
void ApplyRename(App& app);
void CancelRename(App& app);
void DrawIcon(App& app, const VkRect2D& iconRect, VkClearColorValue color, VkDescriptorSet set, bool white = true);
void DrawLine(App& app, VkRect2D scissor, float ax, float ay, float bx, float by, VkClearColorValue color, float halfWidth);  void DrawLetter(App& app, VkRect2D scissor, char c, float cx, float cy, float size, VkClearColorValue color);
void DrawLogicBar(App& app, const Layout& layout, const float* mvp = nullptr);
void DrawMenu(App& app);
void DrawPanel(App& app, const PanelSpec& panel);
void FormatScaleLen(float len, char* buf, int n);
void GetSettingsWPath(wchar_t out[MAX_PATH]);
void OpenRenameEdit(App& app, int index);
void SaveSettingInt(const char* key, int value);
void UpdateBallButtons(App& app, const Layout& layout);
void UpdateNavHud(App& app);
void UpdateObjectLabels(App& app);
void UploadLabelRgba(App& app, App::LabelTexture& lt, const std::vector<uint8_t>& rgba, int w, int h);  // 比例尺**固定长条 + 移动竖线**——横条长度固定，竖线按相机距离 // （对数映射，范围 0.3~10000 与相机 zoom clamp 一致）左右移动指示当前缩放； // 条上方居中显示当前距离数值（RasterizeText 系统字体，窄字 + 明显字间距） static void DrawScaleBar(App& app, const Layout& layout);
void FlushPendingLabelUploads(App& app);  // 帧首安全点处理标签纹理延迟上传（消除 DrawFrame 录制中途 2 秒阻塞回归）

// ---- 错误宏（原 main.cpp 定义，拆分后共享；SetError 见 ui3d.cpp）----
#define VKB_TRY(expr)                                                         \
    do {                                                                      \
        if (VkResult _vkb_res = (expr); _vkb_res != VK_SUCCESS) {             \
            SetError(std::string("Vulkan 调用失败: ") + #expr +               \
                     " (VkResult=" + std::to_string(static_cast<int>(_vkb_res)) + ")"); \
            return false;                                                     \
        }                                                                     \
    } while (0)

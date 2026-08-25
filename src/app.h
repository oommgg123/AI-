// ============================================================================
//   应用类型定义（用户 177 轮：为"专门的导入管线"抽取，供 main.cpp / import_pipeline 共享）
//   App / Grid / UiButton / 按钮状态机 / 按钮主题
// ============================================================================
#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "vulkan_loader.h"
#include "camera.h"
#include "model_import.h"
#include "settings_window.h"
#include "ui_presets.h"   // #214：单主题源 ui::g_theme（COLORREF，GDI 窗口）；ButtonTheme 为其 float 渲染派生

// ---- Round358：面板布局默认尺寸（App 字段默认值 + 分隔线拖拽最小限制共用；ComputeLayout 单一来源）----
constexpr uint32_t kTopBarHeight    = 36;    // 顶栏高（默认=最小）
constexpr uint32_t kSideBarWidth    = 180;   // 右栏宽（默认=最小；160→180 加宽 20px）
constexpr uint32_t kLeftBarWidth    = 43;    // 左栏宽（48→43 再收窄 5px；仅容 30px 按钮+留白）
constexpr uint32_t kBottomBarHeight = 150;   // 底部面板高（默认=最小）

// ---- 按钮交互状态机（唯一的状态转换入口）----
enum class ButtonState : int { Normal = 0, Hover = 1, Pressed = 2, Released = 3 };

struct ButtonStateMachine {
    ButtonState state = ButtonState::Normal;

    bool OnMouseDown(bool inside) {
        if (inside) { state = ButtonState::Pressed; return true; }
        return false;
    }

    void OnMouseUp(bool inside) {
        if (state != ButtonState::Pressed) return;
        state = inside ? ButtonState::Released : ButtonState::Normal;
        OnMouseMove(inside);
    }

    void OnMouseMove(bool inside) {
        if (state == ButtonState::Pressed) {
            // 按下中鼠标移出按钮区域 → 立即取消按下高亮（移回恢复 Pressed），
            // 配合按钮按下 SetCapture：鼠标移出窗口后状态不再滞留、动画不再停止。
            state = inside ? ButtonState::Pressed : ButtonState::Normal;
            return;
        }
        if (inside) {
            if (state == ButtonState::Normal || state == ButtonState::Released) state = ButtonState::Hover;
        } else {
            if (state == ButtonState::Hover || state == ButtonState::Released) state = ButtonState::Normal;
        }
    }
};

struct ButtonTheme {
    float normal[4]     = {0.30f, 0.30f, 0.33f, 1.0f};
    float pressed[4]    = {0.16f, 0.16f, 0.18f, 1.0f};
    float released[4]   = {0.24f, 0.24f, 0.26f, 1.0f};
    float hoverBorder[4] = {1.00f, 1.00f, 1.00f, 1.0f};
    float animSpeed     = 0.25f;
};

// #214：单主题源 = ui::g_theme（COLORREF，GDI 窗口）。Vulkan/GDI 按钮统一用 float 版 ButtonTheme，
// 由 ui::g_theme.button 经此转换器派生（COLORREF 0~255 → float 0~1，含 alpha=1）。
// 用户 button_theme.txt 仍作为逐键覆盖（见 LoadButtonTheme）。
inline ButtonTheme FromUiButtonPreset(const ui::ButtonPreset& p) {
    ButtonTheme t{};
    t.normal[0] = GetRValue(p.normal)   / 255.f; t.normal[1] = GetGValue(p.normal)   / 255.f; t.normal[2] = GetBValue(p.normal)   / 255.f; t.normal[3] = 1.0f;
    t.pressed[0] = GetRValue(p.pressed) / 255.f; t.pressed[1] = GetGValue(p.pressed) / 255.f; t.pressed[2] = GetBValue(p.pressed) / 255.f; t.pressed[3] = 1.0f;
    t.released[0] = GetRValue(p.released) / 255.f; t.released[1] = GetGValue(p.released) / 255.f; t.released[2] = GetBValue(p.released) / 255.f; t.released[3] = 1.0f;
    t.hoverBorder[0] = GetRValue(p.hoverBorder) / 255.f; t.hoverBorder[1] = GetGValue(p.hoverBorder) / 255.f; t.hoverBorder[2] = GetBValue(p.hoverBorder) / 255.f; t.hoverBorder[3] = 1.0f;
    t.animSpeed = p.animSpeed;
    return t;
}

struct App;

struct UiButton {
    VkRect2D rect;
    ButtonStateMachine machine;
    float color[4]{};
    float border[4]{};
    float radius = 4.0f;
    void (*onClick)(App&) = nullptr;
    int icon = 0;
    std::wstring label;              // 按钮内文字（设置/文件/编辑；来自 button_labels.txt，外部可改）
};

// ---- 无限地面网格（Grid push 160B；Draw 声明在此，定义见 main.cpp）----
struct Grid {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    void Draw(VkCommandBuffer cmd, VkBuffer quadBuffer, const float mvp[16],
              const float invVP[16], const Camera& cam, float dynamicGridRadius,
              const float fadeCenter[2], float smallFade);
};

// ---- 应用主状态 ----
// ---- 撤销/重做类型（#209：从 App 内嵌移出为文件级类型，便于复用/独立测试）----
enum class UndoOp : int { Add = 0, Remove = 1, Move = 2, Rotate = 3, Scale = 4 };
struct UndoEntry {
    UndoOp op = UndoOp::Add;
    int index = 0;
    SceneObject obj;                 // Add/Remove 的物体快照（始终副本，不被 move 破坏）
    std::wstring importPath;         // Add=导入时记录源路径（redo 重新导入，避免拷贝大模型）
    std::wstring name;               // Add 记录物体名（redo 重导入后恢复）
    float oldTx = 0, oldTy = 0, oldTz = 0;
    float newTx = 0, newTy = 0, newTz = 0;
    float oldRx = 0, oldRy = 0, oldRz = 0;
    float newRx = 0, newRy = 0, newRz = 0;
    float oldSx = 1, oldSy = 1, oldSz = 1;
    float newSx = 1, newSy = 1, newSz = 1;
};

struct App {
    // ---- 图标纹理（嵌入 PNG → WIC 内存解码→纹理）----
    struct BallIconImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t w = 0, h = 0;
        bool valid = false;
    };
    // ---- 文字标签纹理（RasterizeText→纹理重建）----
    struct LabelTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        int w = 0, h = 0;
        std::wstring text;
        std::vector<uint8_t> pendingRgba;   // 延迟上传暂存（避免 DrawFrame 录制中途阻塞，见 FlushPendingLabelUploads）
        int pendingW = 0, pendingH = 0;
        bool needsUpload = false;
    };

    // ================= Vulkan 上下文（句柄/管线/资源/同步）=================
    struct VulkanCtx {
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;

        uint32_t gpuApiVersion = 0;
        int gpuType = 0;
        uint32_t gpuVendor = 0;   // PCI vendor ID：0x8086 Intel / 0x1002·0x1022 AMD / 0x10DE NVIDIA
        char gpuName[256] = {};

        // 渲染路径：设备支持 1.3 动态渲染则为 true；老显卡/核显自动回退传统 render pass
        bool useDynamicRendering = true;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> framebuffers;

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent{};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;          // 3D pass 用（视口背景/坐标轴十字），采样数随 MSAA
        VkPipeline pipelineUI = VK_NULL_HANDLE;        // 2D 面板 UI pass 用（渲染到 1 采样 swapchain），固定 1 采样
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

        Grid grid;
        VkBuffer vertexBuffer3D = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory3D = VK_NULL_HANDLE;
        VkBuffer indexBuffer3D = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory3D = VK_NULL_HANDLE;
        // 线框专用 40B 顶点缓冲（压缩缓冲与 line3d 40B 布局不匹配）
        VkBuffer wireVtxBuffer3D = VK_NULL_HANDLE;
        VkDeviceMemory wireVtxBufferMemory3D = VK_NULL_HANDLE;
        VkDeviceSize wireVtxBuffer3DCapacity = 0;
        // 复用容量（容量足够时只重写内容不重建 buffer）
        VkDeviceSize vertexBuffer3DCapacity = 0;
        VkDeviceSize indexBuffer3DCapacity = 0;

        VkPipelineLayout menuPipelineLayout = VK_NULL_HANDLE;
        VkPipeline menuPipeline = VK_NULL_HANDLE;

        VkPipelineLayout pipelineLayoutAxis = VK_NULL_HANDLE;
        VkPipeline pipelineAxis = VK_NULL_HANDLE;
        VkPipeline pipelineAxisOccluded = VK_NULL_HANDLE;

        VkImage textImage = VK_NULL_HANDLE;
        VkDeviceMemory textMemory = VK_NULL_HANDLE;
        VkImageView textView = VK_NULL_HANDLE;
        VkSampler textSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout textDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool textDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet textDescriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout textPipelineLayout = VK_NULL_HANDLE;
        VkPipeline textPipeline = VK_NULL_HANDLE;
        int textWidth = 0, textHeight = 0;

        VkImage penImage = VK_NULL_HANDLE;
        VkDeviceMemory penMemory = VK_NULL_HANDLE;
        VkImageView penView = VK_NULL_HANDLE;
        VkDescriptorSet penDescriptorSet = VK_NULL_HANDLE;

        VkImage fileImage = VK_NULL_HANDLE;
        VkDeviceMemory fileMemory = VK_NULL_HANDLE;
        VkImageView fileView = VK_NULL_HANDLE;
        VkDescriptorSet fileDescriptorSet = VK_NULL_HANDLE;

        // 软件图标（awa logo，从 icon.ico 提取 PNG 嵌入；复用 textDescriptorPool）
        VkImage appIconImage = VK_NULL_HANDLE;
        VkDeviceMemory appIconMemory = VK_NULL_HANDLE;
        VkImageView appIconView = VK_NULL_HANDLE;
        VkDescriptorSet appIconDescriptorSet = VK_NULL_HANDLE;

        VkImage importImage = VK_NULL_HANDLE;
        VkDeviceMemory importMemory = VK_NULL_HANDLE;
        VkImageView importView = VK_NULL_HANDLE;
        VkDescriptorSet importDescriptorSet = VK_NULL_HANDLE;

        VkImage exportImage = VK_NULL_HANDLE;
        VkDeviceMemory exportMemory = VK_NULL_HANDLE;
        VkImageView exportView = VK_NULL_HANDLE;
        VkDescriptorSet exportDescriptorSet = VK_NULL_HANDLE;

        VkPipelineLayout pipelineLayoutSolid = VK_NULL_HANDLE;
        VkPipeline pipelineSolid = VK_NULL_HANDLE;         // 16bit 颜色（R16G16B16A16）
        VkPipeline pipelineSolidNoColor = VK_NULL_HANDLE;  // 无颜色（24B 紧凑）
        VkPipeline pipelineSolid8 = VK_NULL_HANDLE;        // 8bit 颜色
        VkPipeline pipelineSolid4 = VK_NULL_HANDLE;        // 4bit 颜色
        VkPipeline pipelineSolid1 = VK_NULL_HANDLE;        // 1bit 颜色
        VkPipeline pipelineSolidMask = VK_NULL_HANDLE;     // 描边 mask pass 专用（固定 1 采样，渲染到 1x outlineImage）

        // ---- 3D 物体选中/线框（Blender 风格）----
        VkPipelineLayout pipelineLayoutLine3d = VK_NULL_HANDLE;
        VkPipeline pipelineLine3d = VK_NULL_HANDLE;        // LINE_LIST 3D 线（高亮框/线框预览）
        VkPipeline pipelineLine3dWide = VK_NULL_HANDLE;    // LINE_LIST 宽线 2px（线框模式加粗）
        VkBuffer selVtxBuffer = VK_NULL_HANDLE;            // 选中高亮临时顶点缓冲（AABB 24 顶点 / 边缘黄线）
        VkDeviceMemory selVtxMem = VK_NULL_HANDLE;
        VkDeviceSize selVtxCapacity = 0;                   // 黄线缓冲容量（不足时重建）
        int selVtxGenIndex = -1;                           // 已生成黄线的物体索引
        std::wstring selVtxGenName;                        // 已生成黄线的物体名（防 index 复用误用）

        // ---- 移动三向标（选中物体显示 X/Y/Z 箭头，可拖拽）----
        VkPipeline pipelineLine3dNoDepth = VK_NULL_HANDLE;  // 无深度测试线（三向标：始终可见）
        VkPipeline pipelineLine3dNoDepthWide = VK_NULL_HANDLE;  // 无深度宽线 2px（旋转 gizmo 环加粗）
        VkPipeline pipelineGizmoSolid = VK_NULL_HANDLE;     // 三向标锥头实体填充（TRIANGLE_LIST 无深度）
        VkBuffer gizmoVtxBuffer = VK_NULL_HANDLE;           // 三向标线框顶点缓冲（中心环+轴主线，每帧重建）
        VkDeviceMemory gizmoVtxMem = VK_NULL_HANDLE;
        VkBuffer gizmoSolidVtxBuffer = VK_NULL_HANDLE;      // 三向标锥头三角顶点缓冲（每帧重建）
        VkDeviceMemory gizmoSolidVtxMem = VK_NULL_HANDLE;

        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        VkImage msaaColorImage = VK_NULL_HANDLE;
        VkDeviceMemory msaaColorMemory = VK_NULL_HANDLE;
        VkImageView msaaColorView = VK_NULL_HANDLE;

        VkImage fxaaImage = VK_NULL_HANDLE;
        VkDeviceMemory fxaaMemory = VK_NULL_HANDLE;
        VkImageView fxaaView = VK_NULL_HANDLE;
        VkSampler fxaaSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout fxaaDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool fxaaDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet fxaaDescriptorSet = VK_NULL_HANDLE;

        // 选中物体屏幕投影描边——mask RT（选中物体白色）+ Sobel 后处理
        VkImage outlineImage = VK_NULL_HANDLE;
        VkDeviceMemory outlineMemory = VK_NULL_HANDLE;
        VkImageView outlineView = VK_NULL_HANDLE;
        VkImage outlineDepthImage = VK_NULL_HANDLE;      // mask pass 专用深度（1x，独立于主帧）
        VkDeviceMemory outlineDepthMemory = VK_NULL_HANDLE;
        VkImageView outlineDepthView = VK_NULL_HANDLE;
        VkSampler outlineSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout outlineDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool outlineDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet outlineDescriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout outlinePipelineLayout = VK_NULL_HANDLE;
        VkPipeline outlinePipeline = VK_NULL_HANDLE;
        VkPipelineLayout fxaaPipelineLayout = VK_NULL_HANDLE;
        VkPipeline fxaaPipeline = VK_NULL_HANDLE;
        VkPipeline pipelinePanelBlend = VK_NULL_HANDLE;   // 框选矩形半透明管线（SRC_ALPHA 混合）

        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };
    VulkanCtx vk;

    // ================= UI 状态（布局/按钮/菜单/标签/HUD）=================
    struct UiState {
        // 面板可调尺寸（拖分隔线调整；min=默认值，max 保证视口 >=60px；ComputeLayout 读取）
        uint32_t panelTopH = kTopBarHeight;         // 顶栏高（可拖 36↑）
        uint32_t panelLeftW = kLeftBarWidth;        // 左栏宽（可拖 53↑；移动/旋转/缩放垂直按钮条）
        uint32_t panelRightW = kSideBarWidth;       // 右栏宽（可拖 160↑）
        uint32_t panelBottomH = kBottomBarHeight;   // 底部面板高（可拖 150↑）
        int resizeDrag = -1;                        // 分隔线拖拽：0=顶栏下缘 1=左栏右缘 2=右栏左缘 3=底栏上缘；-1=无
        float resizeStartMouse = 0.0f;              // 拖拽起始鼠标（水平线用 y，垂直线用 x）
        uint32_t resizeStartVal = 0;                // 拖拽起始面板尺寸

        ButtonTheme buttonTheme;
        std::vector<UiButton> buttons;
        int pressedButton = -1;

        // 顶栏自绘控件（无边框全屏窗口模式：软件图标 + 窗口控制按钮 + 分割线）
        UiButton appIcon;                    // 左上角软件图标（特殊按钮，无边框；复用齿轮图标）
        UiButton sysButtons[3];              // 右上角：最小化/最大化/关闭（PS 风格）
        std::vector<VkRect2D> topDividers;   // 顶栏图标间竖直分割线（ComputeTopBar 计算，DrawLogicBar 绘制）
        bool maximized = false;              // 当前是否全屏铺满主显示器
        bool pendingRestore = false;         // 最大化下按下未达 25px 阈值，延后还原（单击不动 = 不还原）
        bool minimized = false;              // 当前是否最小化（最小化期间停止渲染，避免交换链操作崩溃）
        bool captionDragging = false;        // 标题栏拖拽中（自定义拖拽：SetCapture 后由 WM_MOUSEMOVE 移动窗口）
        bool edgeResizing = false;           // 边缘缩放中（自定义缩放：1=左 2=右 3=底 4=左下 5=右下）
        int  resizeEdge = 0;                 // 当前缩放边缘（0=无）
        int  dragStartX = 0, dragStartY = 0; // 拖拽起点（屏幕坐标，取自 WM_NCLBUTTONDOWN 的 lParam）
        RECT dragOrigRect{0, 0, 0, 0};       // 拖拽起点窗口矩形（屏幕坐标）
        bool appIconDown = false;            // 软件图标按下中
        int pressedSys = -1;                 // 按下的系统按钮索引（0=最小化 1=最大化 2=关闭）
        RECT normalRect{0, 0, 0, 0};         // 还原（非全屏）时的窗口矩形

        // 3D 视口左上角 3 个圆形按钮（标准按钮动画；矩形随布局每帧同步）
        UiButton ballButtons[3];
        // 球按钮图标 + 顶栏变换按钮图标（嵌入 PNG，WIC 内存解码→纹理；描述集复用 textDescriptorPool）
        BallIconImage ballIcons[3];
        BallIconImage transformIcons[3];
        uint32_t ballRefW = 0, ballRefH = 0;   // 球按钮缩放基准（首次视口尺寸，后续按比例缩小至 1/2）
        int pressedBall = -1;
        int renderMode = 0;   // 渲染模式：0=实体 1=线框；默认实体
        int gizmoMode = 0;    // 变换模式 0=移动 1=旋转 2=缩放（顶栏 3 按钮切换）

        bool menuOpen = false;
        float menuAnim = 0.0f;
        uint64_t menuAnimStartMs = 0;
        float menuAnimFrom = 0.0f;
        VkRect2D menuRect{};
        UiButton menuItems[2];
        int pressedMenuItem = -1;

        bool pendingImport = false;
        HWND renameEdit = nullptr;   // 双击物体栏名字 → 内联改名 EDIT 子控件
        int renameIndex = -1;        // 待改名物体索引（-1=无）
        uint64_t pendingImportAtMs = 0;
        uint64_t importBarStartMs = 0;
        float importDisplayProg = 0.0f;

        // 接口保留（shader/管线全保留），当前默认隐藏
        bool axisVisible = false;

        // 物体显示栏文字标签（选中变化时 RasterizeText→纹理重建）
        LabelTexture objNameLabel;   // 当前物体名称
        LabelTexture objPanelTitle;  // 右部物体栏标题条文字（"物体列表"，卡片控件方案B）
        LabelTexture objNameHighlight; // 选中行加粗高亮文字（保留声明，当前已不用）
        int objScroll = 0;           // 物体栏滚动偏移（行单位，超出 5 栏后滚轮/滑块滚动）

        // 物体栏单击/双击判定（0.2s）：文字范围内单击第一下不立即选中，0.2s 内第二击→重命名；超时→补选中
        int objClickRow = -1;        // 待判定行（-1=无）
        uint64_t objClickMs = 0;     // 第一击时间戳
        // 物体栏左键框选（拖拽多选）：按下→移动>4px 进入框选；松开结算
        bool objMarquee = false;     // 框选进行中
        bool objMarqueePending = false;  // 按下未确认框选（等待移动判定）
        float objMx0 = 0, objMy0 = 0;    // 框选起点
        float objMx1 = 0, objMy1 = 0;    // 框选当前点
        LabelTexture scaleLabel;     // 缩放条数值文字（系统默认字体 Segoe UI）
        LabelTexture importUpLabel;  // GPU 上传中提示文字（"正在上传渲染数据…"）
        LabelTexture coordLabels[3] = {};   // 摄像机坐标 X/Y/Z 文字
        LabelTexture buttonLabels[3] = {};  // 顶栏宽按钮文字（设置/文件/编辑；button_labels.txt 可改）
        uint64_t objLabelThrottleMs = 0;   // 标签重建节流（避免拖动时每帧上传纹理）

        // 缩放距离显示（zoom→距离条淡入；停止 0.5s 淡出）
        float navZoomAlpha = 0.0f;      // 缩放距离显示透明度
        float navCoordAlpha = 0.0f;     // 摄像机坐标显示透明度（移动时淡入）
        Vec3 navCoordPrevPos = {};      // 上一帧相机注视点（检测移动）
        uint64_t navCoordStopMs = 0;    // 最后一次平移时刻（停止后静待 1s 再淡出）

        // 中键框选（左键 = orbit 移动视角）
        bool marqueeSelecting = false;         // 框选进行中（中键拖动）
        float marqueeX0 = 0, marqueeY0 = 0;    // 框选起点（屏幕）
        float marqueeX1 = 0, marqueeY1 = 0;    // 框选当前点（屏幕）
        std::vector<int> multiSel;             // 框选多选物体索引（selectedObject 为主操作对象）
        bool lbuttonDown = false;              // 左键按下状态（移动>4px 才进入 orbit，纯点击走拾取）
        uint64_t navLastActionMs = 0;   // 最近一次 orbit/zoom 时间戳
        int navLastActionType = 0;      // 0=无 1=旋转(万向球) 2=缩放(比例尺)
    };
    UiState ui;

    // ================= 场景/选中状态 =================
    struct SceneState {
        std::vector<SceneObject> objects;
        // 颜色位深模式：0=无颜色(最快) 1=16bit 2=8bit 3=4bit 4=1bit
        int vertexColorMode = 0;

        // 当前选中物体索引（-1=无）
        int selectedObject = -1;
        bool wireframeSel = false;   // Tab：选中物体线框预览
        float pressX = 0.0f, pressY = 0.0f;   // 左键按下位置（区分点击/拖拽）
        bool mouseDragged = false;   // 左键是否已拖拽（拖拽=orbit 不拾取）

        // 选中外轮廓（视相关 silhouette）缓存——唯一边端点 + 至多 2 个邻接面法线（边界边 n1={0,0,0} 标记）
        std::vector<uint32_t> selSilA;
        std::vector<uint32_t> selSilB;
        std::vector<float>    selSilN0;
        std::vector<float>    selSilN1;
        int selSilIndex = -1;
        std::wstring selSilName;

        // 小网格（细分线 + 细主格线）渐隐半径（固定默认 150，导入不改变）
        float smallGridFade = 150.0f;
        // 渐隐中心（摄像机位置投影）：每帧由 DrawFrame 按摄像机位置更新
        float fadeCenterXZ[2] = {2.0f, 2.0f};
    };
    SceneState scene;

    // ================= Gizmo 交互状态 =================
    struct GizmoState {
        int gizmoAxis = -1;   // 抓取的轴（0=X 1=Y 2=Z；-1=无）
        int gizmoDragMode = 0;   // 拖动模式：0=无 1=沿轴 3=中心环视口平面自由
        bool gizmoDragging = false;   // 三向标拖动中
        float gizmoLastHit[3] = {0.0f, 0.0f, 0.0f};   // 自由拖拽：上一帧射线-视口平面交点（世界坐标）
        bool gizmoLastHitValid = false;   // 自由拖拽：首次交点是否已记录
        float mouseX = 0.0f, mouseY = 0.0f;   // 当前鼠标客户区坐标（WM_MOUSEMOVE 更新，悬停高亮用）
        float gizmoStartT = 0.0f;   // 拖动起点沿轴参数 t
        float gizmoStartTx = 0.0f, gizmoStartTy = 0.0f, gizmoStartTz = 0.0f;  // 拖动起点物体平移
        float gizmoStartRx = 0.0f, gizmoStartRy = 0.0f, gizmoStartRz = 0.0f;  // 旋转拖动起点欧拉角（度）
        float gizmoStartSx = 1.0f, gizmoStartSy = 1.0f, gizmoStartSz = 1.0f;  // 缩放拖动起点缩放
        float gizmoPivot[3] = {0.0f, 0.0f, 0.0f};   // 拖动起点枢轴点（世界坐标）
        float gizmoScreenPivotX = 0.0f, gizmoScreenPivotY = 0.0f;  // 旋转拖拽枢轴的屏幕投影（角度基准）
    };
    GizmoState gizmo;

    // ================= 抗锯齿状态 =================
    struct AaState {
        AAMode aaMode = AAMode::FXAA;
        bool msaaEnabled = false;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    };
    AaState aa;

    // ================= 撤销/重做栈 =================
    struct UndoState {
        static constexpr int kUndoCapacity = 10;   // 撤销/重做默认存储 10 次（保留改的余地）
        static constexpr int kRedoCapacity = 10;
        std::vector<UndoEntry> undoStack;
        std::vector<UndoEntry> redoStack;
    };
    UndoState undo;

    HWND hwnd = nullptr;
    Camera camera;
    bool running = true;
    bool resizePending = false;
};

// 撤销记录入栈（main.cpp 实现；import_pipeline.cpp 导入时调用）
void PushUndo(App& app, const UndoEntry& e);

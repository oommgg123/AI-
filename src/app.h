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

// ---- Round358：面板布局默认尺寸（App 字段默认值 + 分隔线拖拽最小限制共用；ComputeLayout 单一来源）----
constexpr uint32_t kTopBarHeight    = 36;    // 顶栏高（默认=最小）
constexpr uint32_t kSideBarWidth    = 160;   // 左/右栏宽（默认=最小）
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
        if (state == ButtonState::Pressed) return;
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

struct App;

struct UiButton {
    VkRect2D rect;
    ButtonStateMachine machine;
    float color[4]{};
    float border[4]{};
    float radius = 4.0f;
    void (*onClick)(App&) = nullptr;
    int icon = 0;
};

// ---- 无限地面网格（Grid push 160B；Draw 内联，参数见 main.cpp 调用点）----
struct Grid {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    void Draw(VkCommandBuffer cmd, VkBuffer quadBuffer, const float mvp[16],
              const float invVP[16], const Camera& cam, float dynamicGridRadius,
              const float fadeCenter[2], float smallFade) {
        if (pipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &quadBuffer, &off);
        float push[40];
        std::memset(push, 0, sizeof(push));   // 用户 182 轮 AMD：push constant 未初始化字节驱动敏感（清零防 0xC0000094）
        std::memcpy(push, mvp, 64);
        std::memcpy(push + 16, invVP, 64);
        const Vec3 cr{cam.position.x - cam.target.x,
                      cam.position.y - cam.target.y,
                      cam.position.z - cam.target.z};
        const float dist = std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z);
        // dist 钳零（用户 182 轮 AMD：dist=0 → shader 1/0 → amdvk64 整除崩溃）
        const float distSafe = std::max(dist, 1e-3f);
        // 网格 LOD 采用固定世界间距层级（shader 内 0.125~64 米 10 层，绝对间距制）
        push[32] = 0.0f;   // lodBlend 不再使用
        push[33] = distSafe;  // 相机距离：钳零防 AMD 驱动 1/0 整除崩溃
        push[34] = fadeCenter[0];
        push[35] = fadeCenter[1];
        push[36] = dynamicGridRadius;
        push[37] = smallFade;  // 小网格渐隐半径（150~500），offset 148（在 160B 内）
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 160, push);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
};

// ---- 应用主状态 ----
struct App {
    HWND hwnd = nullptr;
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
    // ---- Round358：面板可调尺寸（拖分隔线调整；min=默认值，max 保证视口 >=60px；ComputeLayout 读取）----
    uint32_t panelTopH = kTopBarHeight;         // 顶栏高（可拖 36↑）
    uint32_t panelLeftW = kSideBarWidth;        // 左栏宽（可拖 160↑）
    uint32_t panelRightW = kSideBarWidth;       // 右栏宽（可拖 160↑）
    uint32_t panelBottomH = kBottomBarHeight;   // 底部面板高（可拖 150↑）
    int resizeDrag = -1;                        // 分隔线拖拽：0=顶栏下缘 1=左栏右缘 2=右栏左缘 3=底栏上缘；-1=无
    float resizeStartMouse = 0.0f;              // 拖拽起始鼠标（水平线用 y，垂直线用 x）
    uint32_t resizeStartVal = 0;                // 拖拽起始面板尺寸
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

    Grid grid;
    VkBuffer vertexBuffer3D = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory3D = VK_NULL_HANDLE;
    VkBuffer indexBuffer3D = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory3D = VK_NULL_HANDLE;
    // 线框专用 40B 顶点缓冲（Round269：Blender 边框模式；压缩缓冲与 line3d 40B 布局不匹配）
    VkBuffer wireVtxBuffer3D = VK_NULL_HANDLE;
    VkDeviceMemory wireVtxBufferMemory3D = VK_NULL_HANDLE;
    VkDeviceSize wireVtxBuffer3DCapacity = 0;
    // 复用容量（用户 168 轮"导入卡一下"）：容量足够时只重写内容不重建 buffer
    VkDeviceSize vertexBuffer3DCapacity = 0;
    VkDeviceSize indexBuffer3DCapacity = 0;
    std::vector<SceneObject> objects;

    ButtonTheme buttonTheme;
    std::vector<UiButton> buttons;
    int pressedButton = -1;
    int editDividerX = 0;   // Round272：编辑按钮右侧竖直分割线 x 坐标（右边缘+70，Round274 左移 80；0=未设置）

    // 3D 视口左上角 3 个圆形按钮（Round248：标准按钮动画；矩形随布局每帧同步）
    UiButton ballButtons[3];
    // Round277：左上角 3 球按钮图标（嵌入 1/2/3.png，运行时 WIC 内存解码→纹理）
    struct BallIconImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t w = 0, h = 0;
        bool valid = false;
    };
    BallIconImage ballIcons[3];   // 描述集复用 textDescriptorPool（容量 8，已用 5，剩 3 刚好）
    BallIconImage transformIcons[3];   // Round297：顶栏变换按钮图标（4=移动 5=旋转 6=缩放）
    uint32_t ballRefW = 0, ballRefH = 0;   // Round286：球按钮缩放基准（首次视口尺寸，后续按比例缩小至 1/2）
    int pressedBall = -1;
    int renderMode = 0;   // Round266：渲染模式（球1=线框 球2=实体 球3暂定）：0=实体 1=线框；默认实体
    int gizmoMode = 0;    // Round296：变换模式 0=移动 1=旋转 2=缩放（顶栏 3 按钮切换）

    VkPipelineLayout menuPipelineLayout = VK_NULL_HANDLE;
    VkPipeline menuPipeline = VK_NULL_HANDLE;
    bool menuOpen = false;
    float menuAnim = 0.0f;
    uint64_t menuAnimStartMs = 0;
    float menuAnimFrom = 0.0f;
    VkRect2D menuRect{};
    UiButton menuItems[2];
    int pressedMenuItem = -1;

    bool pendingImport = false;
    uint64_t pendingImportAtMs = 0;
    uint64_t importBarStartMs = 0;
    float importDisplayProg = 0.0f;

    // 接口保留（shader/管线/Axis3DPush 全保留），当前默认隐藏
    bool axisVisible = false;

    // 小网格（细分线 + 细主格线）渐隐半径（固定默认 150，导入不改变）
    float smallGridFade = 150.0f;
    // 渐隐中心（摄像机位置投影）：每帧由 DrawFrame 按摄像机位置更新
    float fadeCenterXZ[2] = {2.0f, 2.0f};
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
    // 颜色位深模式（用户 159 轮）：0=无颜色(最快) 1=16bit 2=8bit 3=4bit 4=1bit
    int vertexColorMode = 0;

    // ---- 3D 物体选中/线框（Round237，Blender 风格）----
    VkPipelineLayout pipelineLayoutLine3d = VK_NULL_HANDLE;
    VkPipeline pipelineLine3d = VK_NULL_HANDLE;        // LINE_LIST 3D 线（高亮框/线框预览）
    VkPipeline pipelineLine3dWide = VK_NULL_HANDLE;    // LINE_LIST 宽线 2px（Round270：线框模式加粗）
    VkBuffer selVtxBuffer = VK_NULL_HANDLE;            // 选中高亮临时顶点缓冲（AABB 24 顶点 / 边缘黄线）
    VkDeviceMemory selVtxMem = VK_NULL_HANDLE;
    VkDeviceSize selVtxCapacity = 0;                   // Round306：黄线缓冲容量（不足时重建）
    int selVtxGenIndex = -1;                           // Round306：已生成黄线的物体索引
    std::wstring selVtxGenName;                        // Round306：已生成黄线的物体名（防 index 复用误用）
    // Round359：选中外轮廓（视相关 silhouette）缓存——唯一边端点 + 至多 2 个邻接面法线（边界边 n1={0,0,0} 标记）
    std::vector<uint32_t> selSilA;
    std::vector<uint32_t> selSilB;
    std::vector<float>    selSilN0;
    std::vector<float>    selSilN1;
    int selSilIndex = -1;
    std::wstring selSilName;
    int selectedObject = -1;                            // 当前选中物体索引（-1=无）
    bool wireframeSel = false;                          // Tab：选中物体线框预览
    float pressX = 0.0f, pressY = 0.0f;                 // 左键按下位置（区分点击/拖拽）
    bool mouseDragged = false;                          // 左键是否已拖拽（拖拽=orbit 不拾取）

    // 物体显示栏文字标签（Round249：右侧 500px 面板展示当前物体；选中变化时 RasterizeText→纹理重建）
    struct LabelTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        int w = 0, h = 0;
        std::wstring text;
    };
    LabelTexture objNameLabel;   // 当前物体名称
    LabelTexture scaleLabel;     // 缩放条数值文字（Round331：系统默认字体 Segoe UI）
    LabelTexture coordLabels[3] = {};   // 摄像机坐标 X/Y/Z 文字（Round345）
    uint64_t objLabelThrottleMs = 0;   // 标签重建节流（避免拖动时每帧上传纹理）
    // 缩放距离显示（zoom→距离条淡入；停止 0.5s 淡出）
    float navZoomAlpha = 0.0f;      // 缩放距离显示透明度
    float navCoordAlpha = 0.0f;     // 摄像机坐标显示透明度（Round345，移动时淡入）
    Vec3 navCoordPrevPos = {};      // 上一帧相机注视点（检测移动，Round345）
    uint64_t navCoordStopMs = 0;    // 最后一次平移时刻（Round349：停止后静待 1s 再淡出）
    // Round332：中键框选（左键 = orbit 移动视角）
    bool marqueeSelecting = false;         // 框选进行中（中键拖动）
    float marqueeX0 = 0, marqueeY0 = 0;    // 框选起点（屏幕）
    float marqueeX1 = 0, marqueeY1 = 0;    // 框选当前点（屏幕）
    std::vector<int> multiSel;             // 框选多选物体索引（selectedObject 为主操作对象）
    VkPipeline pipelinePanelBlend = VK_NULL_HANDLE;   // Round330：框选矩形半透明管线（panel 同款 + SRC_ALPHA 混合）
    bool lbuttonDown = false;              // Round332：左键按下状态（移动>4px 才进入 orbit，纯点击走拾取）
    uint64_t navLastActionMs = 0;   // 最近一次 orbit/zoom 时间戳
    int navLastActionType = 0;      // 0=无 1=旋转(万向球) 2=缩放(比例尺)

    // ---- 撤销/重做（Round250：Ctrl+Z 撤销 / Ctrl+B 重做；容量默认 10，可改 kUndoCapacity）----
    enum class UndoOp : int { Add = 0, Remove = 1, Move = 2, Rotate = 3, Scale = 4 };
    struct UndoEntry {
        UndoOp op = UndoOp::Add;
        int index = 0;
        SceneObject obj;                 // Add/Remove 的物体快照（始终副本，不被 move 破坏）
        float oldTx = 0, oldTy = 0, oldTz = 0;   // Move：操作前位置
        float newTx = 0, newTy = 0, newTz = 0;   // Move：操作后位置
        float oldRx = 0, oldRy = 0, oldRz = 0;   // Rotate：操作前欧拉角（度）
        float newRx = 0, newRy = 0, newRz = 0;   // Rotate：操作后欧拉角（度）
        float oldSx = 1, oldSy = 1, oldSz = 1;   // Scale：操作前缩放
        float newSx = 1, newSy = 1, newSz = 1;   // Scale：操作后缩放
    };
    std::vector<UndoEntry> undoStack;
    std::vector<UndoEntry> redoStack;
    static constexpr int kUndoCapacity = 10;   // 撤销/重做默认存储 10 次（保留改的余地）
    static constexpr int kRedoCapacity = 10;

    // ---- 渲染距离（Round259）：固定默认值 150×1.7=255，导入任何模型不改变 ----

    // ---- 移动三向标（Round239，Blender 风格：选中物体显示 X/Y/Z 移动箭头，可拖拽移动）----
    VkPipeline pipelineLine3dNoDepth = VK_NULL_HANDLE;  // 无深度测试线（三向标：渲染于最顶层始终可见）
    VkPipeline pipelineLine3dNoDepthWide = VK_NULL_HANDLE;  // 无深度宽线 2px（Round305：旋转 gizmo 环加粗）
    VkPipeline pipelineGizmoSolid = VK_NULL_HANDLE;     // 三向标锥头实体填充（TRIANGLE_LIST 无深度，Round244）
    VkBuffer gizmoVtxBuffer = VK_NULL_HANDLE;           // 三向标线框顶点缓冲（中心环+轴主线，每帧重建）
    VkDeviceMemory gizmoVtxMem = VK_NULL_HANDLE;
    VkBuffer gizmoSolidVtxBuffer = VK_NULL_HANDLE;      // 三向标锥头三角顶点缓冲（每帧重建）
    VkDeviceMemory gizmoSolidVtxMem = VK_NULL_HANDLE;
    int gizmoAxis = -1;                                 // 抓取的轴（0=X 1=Y 2=Z；-1=无）
    int gizmoDragMode = 0;                              // 拖动模式：0=无 1=沿轴 3=中心环视口平面自由（Round264 白边已删）
    bool gizmoDragging = false;                         // 三向标拖动中
    float gizmoLastHit[3] = {0.0f, 0.0f, 0.0f};         // 自由拖拽：上一帧射线-视口平面交点（世界坐标）
    bool gizmoLastHitValid = false;                     // 自由拖拽：首次交点是否已记录
    float mouseX = 0.0f, mouseY = 0.0f;                 // 当前鼠标客户区坐标（WM_MOUSEMOVE 更新，悬停高亮用）
    float gizmoStartT = 0.0f;                           // 拖动起点沿轴参数 t
    float gizmoStartTx = 0.0f, gizmoStartTy = 0.0f, gizmoStartTz = 0.0f;  // 拖动起点物体平移
    float gizmoStartRx = 0.0f, gizmoStartRy = 0.0f, gizmoStartRz = 0.0f;  // Round296：旋转拖动起点欧拉角（度）
    float gizmoStartSx = 1.0f, gizmoStartSy = 1.0f, gizmoStartSz = 1.0f;  // Round298：缩放拖动起点缩放
    float gizmoPivot[3] = {0.0f, 0.0f, 0.0f};           // 拖动起点枢轴点（世界坐标）
    float gizmoScreenPivotX = 0.0f, gizmoScreenPivotY = 0.0f;  // Round304：旋转拖拽枢轴的屏幕投影（角度基准）

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    AAMode aaMode = AAMode::FXAA;
    bool msaaEnabled = false;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
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
    VkPipelineLayout fxaaPipelineLayout = VK_NULL_HANDLE;
    VkPipeline fxaaPipeline = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    Camera camera;

    bool running = true;
    bool resizePending = false;
};

// 撤销记录入栈（main.cpp 实现；import_pipeline.cpp 导入时调用）
void PushUndo(App& app, const App::UndoEntry& e);

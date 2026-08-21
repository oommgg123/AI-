// ============================================================================
//   专门的导入管线（用户 177 轮：导入导出相关全部集中于此）
//   包含：模型导入（OBJ/STL/glTF·GLB/FBX）、OBJ 导出、贴图导入（普通+程序化）、
//         文件选择对话框、导入工作线程、导入结果应用
// ============================================================================
#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "model_import.h"
#include "mc_blocks.h"   // SceneObject / McBlockGrid / McAtlas（MC 场景构建结果类型）
#include "mc_map_pipeline.h"  // 地图窗口专属地图管线（动态更新）
#include "gdi_util.h"   // gdi::DoubleBuffer（MC 窗口双缓冲 DC 缓存）
#include "ui_button.h"  // UiButton / ButtonStateMachine / ButtonTheme（白边正方体按钮管线）

struct App;

// ---- 模型导入 ----
// 按扩展名分发：.obj/.stl/.gltf/.glb/.fbx（其余按 OBJ 解析）
bool ImportModelFile(const wchar_t* path, SceneObject& out);

// ---- 模型导出 ----
// 导出 OBJ（实体顶点 + 面；无实体时导出线框点）
bool ExportObj(const wchar_t* path, const SceneObject& obj);

// ---- 贴图导入（用户 177 轮：目前仅导入存储，渲染暂不采样）----

// 普通贴图：WIC 解码任意图片（PNG/JPG/BMP/GIF/TIFF…）→ RGBA，填入 SceneObject.tex*
bool LoadTextureFromFile(const wchar_t* path, SceneObject& out);

// 程序化贴图：生成棋盘格/渐变纹理 → RGBA，填入 SceneObject.tex*
enum class ProceduralTexture : int { Checkerboard = 0, Gradient = 1, Grid = 2 };
bool GenerateProceduralTexture(ProceduralTexture kind, int size, SceneObject& out);

// OBJ .mtl 引用的贴图（WIC 解码，失败不影响导入）
void LoadObjTexture(const wchar_t* objPath, const std::string& mtlFile, SceneObject& out);

// ---- 文件选择 ----
// 模型文件对话框（OBJ/STL/glTF/FBX + 全部文件）
bool PickModelFile(HWND owner, wchar_t* outPath, size_t outCap);
// 贴图文件对话框（常用图片格式）
bool PickTextureFile(HWND owner, wchar_t* outPath, size_t outCap);

// ---- 导入工作线程（异步解析，完成后 PostMessage(WM_APP) 通知主线程）----
void LaunchImport(App& app, const wchar_t* path);
void ApplyImportResult(App& app);   // 主线程调用：解析结果 → GPU 上传 → 追加物体（不动摄像机）
void WaitForImportThread();

// ---- 场景装载 ----
void LoadSceneObjects(App& app);    // 启动时加载示例.obj / 命令行指定模型

// 导入进度（0~100，-1 表示无导入任务）；main.cpp 进度条读取
extern std::atomic<int> g_importProgress;
// 我的世界导入专用进度（与模型导入 g_importProgress 分离：地图窗口 loading 时 3D 视口不显示进度条）
extern std::atomic<int> g_mcProgress;

// ============================================================================
//   我的世界（基岩版）地图导入（用户 188 轮：从 mc_world.cpp 整合进导入管线）
//   专用类封装：NBT 解析 / 版本校验（1.12-1.21）/ 400×400 默认色 2D 窗口。
//   调用方式：McWorldImporter::Instance().Validate(path) / .OpenWindow(...)
// ============================================================================
class McWorldImporter {
public:
    struct Validation {
        bool valid = false;          // 是否为 1.12-1.21 基岩版有效世界
        std::wstring versionText;    // 版本文本，如 L"1.21.0" / L"未知版本"
        std::wstring reason;         // 中文说明（成功/失败原因）
    };

    static McWorldImporter& Instance();   // 单例（窗口状态唯一）

    // 验证路径：目录 / .mcworld / level.dat；返回版本有效性
    Validation Validate(const wchar_t* path);

    // 打开 400×400 默认色 2D 窗口（模态于 owner；关闭自动恢复 owner）
    HWND OpenWindow(HWND owner, const wchar_t* versionText);
    void CloseWindow();

    // 加载状态控制（主循环先开窗显示 loading 进度条，后台完成后再更新真实信息）
    void SetLoading(bool loading) { m_loading = loading; }
    bool IsLoading() const { return m_loading; }
    HWND GetHwnd() const { return m_wnd; }
    void UpdateVersion(const wchar_t* ver) { m_ver = ver ? ver : L""; }
    const std::wstring& GetVersion() const { return m_ver; }
    void LoadInfo() { CollectInfo(m_rootDir); }  // Validate 后收集世界信息（供外部调用）

    // 异步加载：先开窗(loading进度条，进度从0%起)→后台线程验证+收集信息→完成向 owner 发 WM_APP+2
    void BeginLoad(HWND owner, const std::wstring& path);
    std::wstring GetLastReason() const { return m_lastValidation.reason; }

    // 后台线程构造成功后，主线程取走数据（Vulkan 上传必须在主线程）
    void TakeBuilt(SceneObject& outObj, McBlockGrid& outGrid, McAtlas& outAtlas) {
        outObj = std::move(m_builtObj);
        outGrid = m_builtGrid;
        outAtlas = m_builtAtlas;
    }
    bool GetBuiltReady() const { return m_builtReady; }

private:
    McWorldImporter() = default;
    McWorldImporter(const McWorldImporter&) = delete;
    McWorldImporter& operator=(const McWorldImporter&) = delete;

    HWND       m_wnd = nullptr;
    HWND       m_owner = nullptr;
    HFONT      m_font = nullptr;
    std::wstring m_ver;              // 版本文本（OpenWindow 入参）
    bool       m_registered = false;
    gdi::DoubleBuffer m_db{};   // 双缓冲 DC（惰性缓存，尺寸变化时重建）

    // 阶段一：真实信息（读取自 level.dat / levelname.txt / db/）
    std::wstring m_rootDir;      // 含 level.dat + db/ 的世界根目录
    std::wstring m_levelDatPath; // level.dat 完整路径
    std::wstring m_worldName;    // 世界名（levelname.txt / LevelName）
    std::wstring m_seed;         // 种子文本
    std::wstring m_spawn;        // 出生点文本
    int          m_dbCount = 0;  // db 文件数
    int64_t      m_dbBytes = 0;  // db 总字节
    std::wstring m_tempDir;      // .mcworld 解压临时目录（关闭时清理）
    bool       m_loading = false; // loading 状态（窗口中央显示进度条）
    std::wstring m_loadPath;       // BeginLoad 传入的待导入路径
    Validation   m_lastValidation; // 后台线程完成的验证结果（供主线程 finalize 使用）

    // 后台线程构建的 3D 场景数据（主线程 finalize 时取走并上传 GPU）
    SceneObject         m_builtObj;
    McBlockGrid         m_builtGrid;
    McAtlas             m_builtAtlas;
    bool                m_builtReady = false;
    static DWORD WINAPI _LoadThread(LPVOID param);  // 后台验证/收集信息线程

    // 从世界根目录收集真实信息（世界名 / 种子 / 出生点 / db 统计）
    void CollectInfo(const std::wstring& rootDir);

    // ---- 2D 地图控件（1px 白色直角白边 + 内部 480×480 方形地图）----
    UiButton            m_viewSquare;       // 480×480 直角方形（radius=0），仅视觉，非交互按钮
    ButtonStateMachine  m_viewSquareSm;     // 状态机（仅驱动颜色动画，无点击行为）
    ButtonTheme         m_viewSquareTheme;  // 纯白填充、直角、悬停黑描边的本地主题
    McMapPipeline       m_mapPipe;          // 地图窗口专属地图管线（Build 后可动态重建/注入）

    static constexpr int      kW = 500, kH = 600;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

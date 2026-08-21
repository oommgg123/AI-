// ============================================================================
//   我的世界方块模型库 + 预拼图集 + 合并网格（Round194）
//   - 通用方块注册表：1.12 数字 id 与 1.13+ 字符串 id 统一映射到同一 key
//   - 预拼图集：从 s1 目录加载 ss1-RGB(+A).png + ss1.mtl（MTL 材质名→UV 网格）
//   - 合并网格：面消隐 + 贪心合并（合并相邻同纹理方块/顶点）+ uv 指向 atlas
//   - 数据源：用户提供的预拼图集目录（含 .png + .mtl），非运行时扫描/程序化
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "model_import.h"   // SceneObject / VertexSolid

// 6 个面（顺序固定，供注册表与图集对齐）
enum McFace : int {
    MC_TOP = 0, MC_BOTTOM = 1, MC_NORTH = 2, MC_SOUTH = 3, MC_EAST = 4, MC_WEST = 5,
    MC_FACE_COUNT = 6
};

// 通用方块定义（尽量全量；缺失方块回退 "unknown"）
struct BlockDef {
    const char* key;                 // 通用 key，如 "minecraft:stone"
    int         legacyId;            // 1.12 数字 id（-1 表示无对应）
    const char* tex[MC_FACE_COUNT];  // 每面贴图 stem（"unknown" 占位）
};

// 注册表访问（线程安全：首次访问惰性构建）
int  McBlockCount();
const BlockDef* McBlockByIndex(int i);

// 解析：返回注册表索引（-1 = 空气 / 未知）
int McResolveLegacy(int legacyId);                       // 1.12 数字 id
int McResolveString(const char* stateId);               // 1.13+ 字符串（自动去 [..] 状态）
int McResolve(const std::string& stateId, int legacyId);// 综合（优先字符串）

// 图集矩形（归一化 uv）
struct McAtlasRect { float u0 = 0, v0 = 0, u1 = 0, v1 = 0; };

// 大图集（一张预拼 PNG 包含全部方块贴图；uv 合并的目标）
struct McAtlas {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;                       // 图集图像 RGBA（ss1-RGB/A 复合）
    std::vector<uint8_t> alpha;                      // 独立 alpha 遮罩（ss1-Alpha，可选）
    std::map<std::string, McAtlasRect> rects;       // 按 MTL 材质名（小写）
    McAtlasRect unknown{ 0, 0, 1, 1 };              // 回退矩形

    // 手工映射表（mc_blockmap.h）派生的反向索引：方块身份 → 图集槽位
    std::vector<std::string> mtlNames;              // 有序材质名（与图集网格顺序一致）
    std::map<std::string, int> keyToSlot;           // 1.13 短 id → 槽位（含变体家族）
    std::map<int, int>        legacyToSlot;          // 1.12 数字 id → 槽位（含 data 的同族）
};

// 从预拼图集目录加载（含 ss1-RGB.png / ss1-RGBA.png / ss1-Alpha.png / ss1.mtl）
//   dir: 指向 s1 目录（如 L"D:\\3D Objects\\模型\\s1"）
//   自动解析 MTL 获取有序材质列表 → 计算 UV 网格 → 加载 PNG → 构建映射表
bool McBuildAtlasFromPrebuilt(const wchar_t* dir, McAtlas& out);

// 方块网格（世界数据；目前由测试世界填充，未来由 db/ 读取填充）
struct McBlockGrid {
    int32_t sx = 0, sy = 0, sz = 0;
    std::vector<int> cells;   // 注册表索引，-1 = 空气
    int  at(int x, int y, int z) const;
    void set(int x, int y, int z, int v);
    bool inRange(int x, int y, int z) const { return x>=0&&y>=0&&z>=0&&x<sx&&y<sy&&z<sz; }
};

// 合成测试世界（演示用；未来用 db/ 真实数据替换 BuildTestWorld 调用处）
void McBuildTestWorld(McBlockGrid& out);

// 方块身份 → 图集矩形（正确处理 1.13 变体家族与 1.12 数字 id；旧回退最后）。
// 供贪心合并网格与地图管线（mc_map_pipeline）共用。
McAtlasRect McRectFor(const McAtlas& a, int block, int face);

// ---- 2D 地图辅助（Round231b）----
// 方块注册表索引 → 颜色 0xRRGGBB。接口保留（渲染已走纹理平铺，当前返回默认色）。
uint32_t McBlockColor(int blockIndex);

// 该方块是否属于 MCA 的 transparent 集合（air/barrier/light/structure_void）。
// 2D 取顶时跳过（与 MCA ChunkRenderer 一致）。
bool McBlockIsTransparent(int blockIndex);

// 贪心合并网格：面消隐 + 共线同纹理面合并 + 顶点合并；输出合并几何
//   out.solidVerts：pos/normal/color（白色=全亮度，贴图采样时乘以纹理颜色）
//   out.mcUv：与 solidVerts 一一对应（2 float/顶点），uv 指向 atlas
//   out.texRgba/Width/Height：图集图像（由本函数按 useMca 设置）
//   useMca：true = MCA 颜色表单色渲染（顶点色=方块色，图集=1×1 白，规避 ss1 图集错位）；
//           false = ss1 纹理图集渲染（顶点色全白，uv 指向图集）。默认 true。
void McBuildMergedMesh(const McBlockGrid& grid, const McAtlas& atlas, SceneObject& out,
                       bool useMca = true);

// 端到端：构建测试世界 + 图集 + 合并网格 → out（供导入流程"读取到就自动加载"调用）
//   outGrid / outAtlas 可选输出（供小地图 / 调试使用；传 nullptr 则忽略）
bool McBuildDemoWorld(SceneObject& out, McBlockGrid* outGrid = nullptr, McAtlas* outAtlas = nullptr);

// ============================================================================
//   3D 模型导入（用户 176 轮：新增 STL / glTF·GLB / FBX 格式支持）
//   - OBJ 解析（ParseOBJ）本就在此模块：用户 177 轮自 main.cpp 迁入，188 轮与 STL/GLTF/FBX 归并至此
//   - 本模块提供 OBJ / STL / glTF / FBX 解析，返回统一 SceneObject
//   - 无第三方库：glTF 用手写轻量 JSON 解析器；FBX 解析 7500+ 节点树基础几何
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// 顶点含颜色（用户 156 轮否决瘦身：以后要做带颜色的光照渲染，颜色必须留在顶点里）
struct VertexSolid {
    float pos[3];
    float normal[3];
    float color[4];
};

// Round369：线框顶点瘦身——只存坐标（12B/顶点，原 VertexSolid 40B；颜色/法线在上传 GPU 时补全）
struct WirePos {
    float pos[3];
};

struct SceneObject {
    std::wstring name;
    std::vector<WirePos> wireVerts;
    std::vector<uint32_t> wireIndices;
    std::vector<VertexSolid> featureVerts;   // Round309：特征边（棱边+边界边，Blender Freestyle 参考）——选中外框用
    std::vector<VertexSolid> solidVerts;
    std::vector<uint32_t> solidIndices;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    // Round296：旋转（欧拉角，度）与缩放（默认 1）——渲染 model = T·R·S，接口 RotateSelectedObject/ScaleSelectedObject
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
    uint32_t vertexOffset = 0;
    uint32_t solidIndexOffset = 0;
    uint32_t wireIndexOffset = 0;      // 线框索引在合并索引缓冲中的偏移（Round237）
    uint32_t wireVtxOffset = 0;        // 线框顶点在专用 40B 缓冲中的偏移（Round269 Blender 边框）
    float boundsMin[3] = {1e30f, 1e30f, 1e30f};   // 物体 AABB（含网格顶点，未含 tx/ty/tz 平移）
    float boundsMax[3] = {-1e30f, -1e30f, -1e30f};
    std::vector<uint8_t> texRgba;
    int texWidth = 0, texHeight = 0;
    // 文件顶点法线（OBJ 的 vn 行，用户 166 轮"法线渲染接口"）；hasFileNormals 表示文件带法线
    std::vector<std::array<float, 3>> vn;
    bool hasFileNormals = false;
    // 我的世界合并网格专用：与 solidVerts 一一对应的 atlas UV（2 float/顶点）
    // 即使目前不渲染贴图，也先把 uv 合并成一张大图集存好（Round193）
    std::vector<float> mcUv;
};

// 计算物体 AABB（遍历 solidVerts 顶点；写回 boundsMin/boundsMax）
void ComputeObjectBounds(SceneObject& o);

// STL：自动识别二进制（80B 头 + 三角面）与 ASCII（solid/facet 文本）
bool ImportSTL(const wchar_t* path, SceneObject& out);

// glTF/GLB：.glb 二进制容器 或 .gltf JSON + 外部 .bin；提取几何（POSITION/索引/NORMAL）
bool ImportGLTF(const wchar_t* path, SceneObject& out);

// FBX：ASCII 文本 或 二进制 7500+ 节点树；提取 Geometry 节点 Vertices/PolygonVertexIndex
bool ImportFBX(const wchar_t* path, SceneObject& out);

// OBJ：Wavefront .obj（用户 177 轮自 main.cpp 迁入；188 轮与 STL/GLTF/FBX 归并至本模块）
bool ParseOBJ(const wchar_t* path, SceneObject& out);
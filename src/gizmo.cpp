// ============================================================================
//  gizmo.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"

using std::uint32_t;

static const float kUserRed[3] = {0.765f, 0.055f, 0.137f}; // 5.png 旋转红
static const float kUserGreen[3] = {0.137f, 0.675f, 0.224f}; // 6.png 缩放绿
static const float kUserBlue[3] = {0.114f, 0.125f, 0.533f}; // 6.png 缩放蓝

void MatMul4(const float a[16], const float b[16], float out[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = s;
        }
    }
}
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
 MatMul4(Rz, tmp, R); // R = Rz·Ry·Rx
    const float S[16] = {o.sx,0,0,0, 0,o.sy,0,0, 0,0,o.sz,0, 0,0,0,1};
    float RS[16];
 MatMul4(R, S, RS); // RS = R·S缩放在旋转前 → 本地空间缩放，沿物体本地方向生效
    const float T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, o.tx,o.ty,o.tz,1};
 MatMul4(T, RS, out); // out = T·R·S（缩放本地化：先缩放后旋转，缩放随物体朝向改变）
}
void BuildRotFromEuler(float rx, float ry, float rz, float R[16]) {
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
 MatMul4(Rz, tmp, R); // R = Rz·Ry·Rx（列主序）
}
void MakeWorldRot(int axis, float deg, float R[16]) {
    const float r = deg * 3.14159265f / 180.0f;
    const float c = std::cos(r), s = std::sin(r);
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
 if (axis == 0) { // 绕世界 X
        m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
 } else if (axis == 1) { // 绕世界 Y
        m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
 } else { // 绕世界 Z
        m[0] = c; m[1] = s; m[4] = -s; m[5] = c;
    }
    for (int i = 0; i < 16; ++i) R[i] = m[i];
}
void EulerFromR(const float R[16], float& rx, float& ry, float& rz) {
 const float sy = -R[2]; // R[2] = -sin(ry)
    float ryv = std::asin(std::max(-1.0f, std::min(1.0f, sy)));
    const float cy = std::cos(ryv);
    if (cy > 1e-6f) {
 rz = std::atan2(R[1], R[0]) * 180.0f / 3.14159265f; // atan2(sin(rz),cos(rz))
 rx = std::atan2(R[6], R[10]) * 180.0f / 3.14159265f; // atan2(sin(rx),cos(rx))
 } else { // 万向锁（ry≈±90°）：固定 rz=0，由残余解 rx
        rz = 0.0f;
        rx = std::atan2(R[4], R[5]) * 180.0f / 3.14159265f;
    }
    ry = ryv * 180.0f / 3.14159265f;
}
void RotateSelectedObject(App& app, char axis, float deg) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    SceneObject& so = app.scene.objects[app.scene.selectedObject];
    UndoEntry e;
    e.op = UndoOp::Rotate;
    e.index = app.scene.selectedObject;
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
void ScaleSelectedObject(App& app, float factor) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    if (factor <= 0.0f) return;
    SceneObject& so = app.scene.objects[app.scene.selectedObject];
    UndoEntry e;
    e.op = UndoOp::Scale;
    e.index = app.scene.selectedObject;
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
void GizmoFillVert(VertexSolid& vv, const float* pos, const float* col) {
    vv.pos[0] = pos[0]; vv.pos[1] = pos[1]; vv.pos[2] = pos[2];
    vv.normal[0] = 0.0f; vv.normal[1] = 1.0f; vv.normal[2] = 0.0f;
    vv.color[0] = col[0]; vv.color[1] = col[1]; vv.color[2] = col[2]; vv.color[3] = 1.0f;
}
void GizmoPerpBasis(const float d[3], float u[3], float w[3]) {
 float rx = 0.0f, ry = 1.0f, rz = 0.0f; // 参考向量（优先 Y，与 d 近平行时改 X）
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
void GizmoFillCone(const float* tip, const float* base, float rad,
                          const float* u, const float* w,
                          VertexSolid* v, int& vi, int n, const float* col);

// 创建并绑定 HOST 可见顶点缓冲惰性：首次创建复用；容量不足时销毁重建，
// 因旋转/缩放 gizmo 与移动三向标共用缓冲且顶点数不同
bool EnsureHostVtxBuffer(App& app, VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize size) {
    if (buf != VK_NULL_HANDLE) {
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(app.vk.device, buf, &mr);
        if (mr.size >= size) return true;
        vkDestroyBuffer(app.vk.device, buf, nullptr);
        vkFreeMemory(app.vk.device, mem, nullptr);
        buf = VK_NULL_HANDLE;
        mem = VK_NULL_HANDLE;
    }
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(app.vk.device, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(app.vk.device, buf, &mr);
    const uint32_t mi = FindMemoryType(app, mr.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mi;
    if (vkAllocateMemory(app.vk.device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(app.vk.device, buf, mem, 0);
    return true;
}
void DrawMoveGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    if (app.vk.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.vk.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）

 // 本地轴配色（与缩放 gizmo 的 kUserRed/Green/Blue 一致；该常量定义在下方，此处本地定义避免顺序问题）
    static const float kMvRed[3]   = {0.765f, 0.055f, 0.137f};
    static const float kMvGreen[3] = {0.137f, 0.675f, 0.224f};
    static const float kMvBlue[3]  = {0.114f, 0.125f, 0.533f};
    const float cols[3][3] = {{kMvRed[0], kMvRed[1], kMvRed[2]},
                              {kMvGreen[0], kMvGreen[1], kMvGreen[2]},
                              {kMvBlue[0], kMvBlue[1], kMvBlue[2]}};
    const bool dragging = (app.gizmo.gizmoDragging && app.gizmo.gizmoDragMode == 1);
    const int hover = (!dragging) ? PickGizmoAxisAt(app, app.gizmo.mouseX, app.gizmo.mouseY) : -1;
 // ---- 线框：3 条轴主线（p → 锥底）----
    constexpr int kLineVerts = 3 * 2;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoVtxBuffer, app.vk.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmo.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len * 0.82f,
                                  p[1] + dirs[i][1] * len * 0.82f,
                                  p[2] + dirs[i][2] * len * 0.82f};
            GizmoFillVert(v[vi++], p, col);
            GizmoFillVert(v[vi++], tip, col);
        }
        vkUnmapMemory(app.vk.device, app.vk.gizmoVtxMem);
    }
 // ---- 实体：3 锥头 + 中心方块 ----
    constexpr int kConeVerts = 12 * 6;
    constexpr int kCubeVerts = 12 * 3;
    constexpr int kSolidVerts = kConeVerts * 3 + kCubeVerts;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoSolidVtxBuffer, app.vk.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmo.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            const float base[3] = {p[0] + dirs[i][0] * len * 0.82f,
                                   p[1] + dirs[i][1] * len * 0.82f,
                                   p[2] + dirs[i][2] * len * 0.82f};
            float perpU[3], perpW[3];
 GizmoPerpBasis(dirs[i], perpU, perpW); // 本地轴垂直基（任意朝向正确截面）
            GizmoFillCone(tip, base, len * 0.05f, perpU, perpW, v, vi, 12, col);
        }
 const float hs = len * 0.085f; // 中心方块（自由拖拽把手）
        bool centerHit = (app.gizmo.gizmoDragging && app.gizmo.gizmoDragMode == 3);
        if (!centerHit) centerHit = HitGizmoRingAt(app, app.gizmo.mouseX, app.gizmo.mouseY);
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
        vkUnmapMemory(app.vk.device, app.vk.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineLine3dNoDepth);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kSolidVerts, 1, 0, 0);
}
void GizmoFillSphere(const float* c, float rad, VertexSolid* v, int& vi,
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
void GizmoFillCone(const float* tip, const float* base, float rad,
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
void GizmoFillCube(const float* c, float hs, VertexSolid* v, int& vi, const float* col) {
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
void DrawRotateGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    if (app.vk.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.vk.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp);
    const float r = len * 0.78f;
 // 世界轴环基（gizmo 固定不随物体旋转）：0=X 环(YZ) 红 / 1=Y 环(XZ) 绿 / 2=Z 环(XY) 蓝
    float basis[3][2][3] = {
        {{0, 1, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 1, 0}},
    };
    const float cols[3][3] = {{kUserRed[0], kUserRed[1], kUserRed[2]},
                              {kUserGreen[0], kUserGreen[1], kUserGreen[2]},
                              {kUserBlue[0], kUserBlue[1], kUserBlue[2]}};
    const bool dragging = (app.gizmo.gizmoDragging && app.gizmo.gizmoDragMode == 4);
    const int dragAxis = dragging ? app.gizmo.gizmoAxis : -1;
 // Blender 式悬停高亮——未拖拽时按鼠标位置检测命中环/手柄/中心（-2=中心 trackball）
    const int hover = (!dragging) ? PickRotateGizmoAt(app, app.gizmo.mouseX, app.gizmo.mouseY) : -1;
    const int hi = dragging ? dragAxis : hover;
 // ---- 线框：3 环（64 段/环，已删除拖拽弧线箭头）----
    constexpr int kSegs = 64;
    constexpr int kLineVerts = 3 * kSegs * 2;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoVtxBuffer, app.vk.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
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
        vkUnmapMemory(app.vk.device, app.vk.gizmoVtxMem);
    }
 // ---- 实体：3 环 × 4 象限手柄球 + 中心轴点 ----
    constexpr int kSphereVerts = 8 * 6 * 6;
    constexpr int kSolidVerts = kSphereVerts * 13;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoSolidVtxBuffer, app.vk.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
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
 GizmoFillSphere(p, r * 0.09f, v, vi, 8, 6, centerCol); // 中心轴点放大（可点击 trackball）
        vkUnmapMemory(app.vk.device, app.vk.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
 // 旋转环 2px 宽线（无深度）；驱动不支持宽线时回退 1px
    const VkPipeline ringPipe = (app.vk.pipelineLine3dNoDepthWide != VK_NULL_HANDLE)
                                    ? app.vk.pipelineLine3dNoDepthWide : app.vk.pipelineLine3dNoDepth;
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ringPipe);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kSolidVerts, 1, 0, 0);
}
void DrawScaleGizmo(App& app, const float mvp[16], const VkRect2D& vp) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return;
    if (app.vk.pipelineLine3dNoDepth == VK_NULL_HANDLE || app.vk.pipelineGizmoSolid == VK_NULL_HANDLE) return;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）

    const float cols[3][3] = {{kUserRed[0], kUserRed[1], kUserRed[2]},
                              {kUserGreen[0], kUserGreen[1], kUserGreen[2]},
                              {kUserBlue[0], kUserBlue[1], kUserBlue[2]}};
    const bool dragging = (app.gizmo.gizmoDragging && app.gizmo.gizmoDragMode == 5);
 // Blender 式悬停高亮——未拖拽时按鼠标位置检测命中轴/锥头/中心方块（3=中心）
    const int hover = (!dragging) ? PickScaleGizmoAt(app, app.gizmo.mouseX, app.gizmo.mouseY) : -1;
 // ---- 线框：3 条轴主线（p → 轴端立方体）----
    constexpr int kLineVerts = 3 * 2;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoVtxBuffer, app.vk.gizmoVtxMem, kLineVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoVtxMem, 0, kLineVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmo.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            GizmoFillVert(v[vi++], p, col);
            GizmoFillVert(v[vi++], tip, col);
        }
        vkUnmapMemory(app.vk.device, app.vk.gizmoVtxMem);
    }
 // ---- 实体：3 个小立方体（轴端把手）+ 中心方块缩放外观=小立方体，区别于移动锥头----
    constexpr int kEndCubeVerts = 12 * 3;
    constexpr int kCenterCubeVerts = 12 * 3;
    constexpr int kSolidVerts = kEndCubeVerts * 3 + kCenterCubeVerts;
    if (!EnsureHostVtxBuffer(app, app.vk.gizmoSolidVtxBuffer, app.vk.gizmoSolidVtxMem, kSolidVerts * 40)) return;
    {
        void* mapped = nullptr;
        if (vkMapMemory(app.vk.device, app.vk.gizmoSolidVtxMem, 0, kSolidVerts * 40, 0, &mapped) != VK_SUCCESS) return;
        VertexSolid* v = static_cast<VertexSolid*>(mapped);
        int vi = 0;
 const float hs2 = len * 0.07f; // 轴端小立方体半边长
        for (int i = 0; i < 3; ++i) {
            const bool active = (dragging && app.gizmo.gizmoAxis == i) || (!dragging && hover == i);
            float col[4] = {cols[i][0], cols[i][1], cols[i][2], 1.0f};
            if (active) for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, col[c] * 1.5f + 0.2f);
            const float tip[3] = {p[0] + dirs[i][0] * len,
                                  p[1] + dirs[i][1] * len,
                                  p[2] + dirs[i][2] * len};
            GizmoFillCube(tip, hs2, v, vi, col);
        }
        float cb[4] = {0.96f, 0.96f, 1.0f, 1.0f};
 if ((dragging && app.gizmo.gizmoAxis == 3) || (!dragging && hover == 3)) { // 拖/悬停中心方块提亮
            for (int c = 0; c < 3; ++c) cb[c] = 1.0f;
        }
 GizmoFillCube(p, len * 0.085f, v, vi, cb); // 中心方块（等比把手）
        vkUnmapMemory(app.vk.device, app.vk.gizmoSolidVtxMem);
    }
    const VkViewport gvp{static_cast<float>(vp.offset.x), static_cast<float>(vp.offset.y),
                         static_cast<float>(vp.extent.width), static_cast<float>(vp.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(app.vk.commandBuffer, 0, 1, &gvp);
    vkCmdSetScissor(app.vk.commandBuffer, 0, 1, &vp);
    VkDeviceSize off = 0;
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineLine3dNoDepth);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kLineVerts, 1, 0, 0);
    vkCmdBindPipeline(app.vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app.vk.pipelineGizmoSolid);
    vkCmdBindVertexBuffers(app.vk.commandBuffer, 0, 1, &app.vk.gizmoSolidVtxBuffer, &off);
    vkCmdPushConstants(app.vk.commandBuffer, app.vk.pipelineLayoutLine3d,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, mvp);
    vkCmdDraw(app.vk.commandBuffer, kSolidVerts, 1, 0, 0);
}

// ==================== 旋转/缩放 gizmo（Blender 式：3 模式不同样式） ====================

// 旋转 gizmo 的 3 个彩色圆环（绕世界 X/Y/Z 轴）——与移动三向标样式不同
// ==================== 旋转/缩放 gizmo 重做（参考 Blender 外观，颜色用用户 4/5/6.png 三色） ====================
// ============================================================================
// 旋转 / 缩放 gizmo 重做（外观参考 Blender 源码效果，颜色采用 4/5/6.png 三色）
// 旋转（红轴 X）：完整圆环 + 4 象限手柄球 + 中心轴点；拖拽中显示弧线箭头（Blender 样式）
// 缩放（等比）：XYZ 三轴 + 轴末端锥形箭头 + 中心方块把手（Blender 缩放 gizmo 样式）
// ============================================================================


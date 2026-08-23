// ============================================================================
//  picking.cpp（#208 拆分：原 main.cpp ）
// ============================================================================
#include "awa_internal.h"

using std::uint32_t;

float RayAABB(const float* o, const float* d,
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
bool BuildViewRay(App& app, float mx, float my, float o[3], float d[3]) {
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
bool RayTriangle(const float* o, const float* d,
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
int PickObjectAt(App& app, float mx, float my) {
    float o[3], d[3];
    if (!BuildViewRay(app, mx, my, o, d)) return -1;
    int best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < static_cast<int>(app.scene.objects.size()); ++i) {
        const SceneObject& ob = app.scene.objects[i];
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
 if (tLocal >= 1e29f) { // 无三角命中：纯线框/无面物体回退 AABB 最近点
            tLocal = RayAABB(ro, d, ob.boundsMin[0], ob.boundsMin[1], ob.boundsMin[2],
                             ob.boundsMax[0], ob.boundsMax[1], ob.boundsMax[2]);
            if (tLocal < 0.0f) continue;
        }
        if (tLocal < bestT) { bestT = tLocal; best = i; }
    }
    return best;
}
float DistPointToSeg2D(float px, float py, float ax, float ay, float bx, float by) {
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
bool ClosestAxisParam(const float a[3], const float dir[3],
                             const float o[3], const float d[3], float& t) {
    const float B = dir[0] * d[0] + dir[1] * d[1] + dir[2] * d[2];
    const float C = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    const float w0[3] = {a[0] - o[0], a[1] - o[1], a[2] - o[2]};
    const float W = dir[0] * w0[0] + dir[1] * w0[1] + dir[2] * w0[2];
    const float V = d[0] * w0[0] + d[1] * w0[1] + d[2] * w0[2];
 const float denom = C - B * B; // dir 单位 → A=1
 if (std::fabs(denom) < 1e-8f) { t = 0.0f; return false; } // 视线与轴近平行：不稳定
    t = (V * B - W * C) / denom;
    return true;
}
void GizmoPivot(const SceneObject& o, float out[3]) {
    out[0] = (o.boundsMin[0] + o.boundsMax[0]) * 0.5f + o.tx;
    out[1] = (o.boundsMin[1] + o.boundsMax[1]) * 0.5f + o.ty;
    out[2] = (o.boundsMin[2] + o.boundsMax[2]) * 0.5f + o.tz;
}
float GizmoAxisLen(App& app, const float pivot[3], const VkRect2D& vp) {
    if (vp.extent.height <= 0) return 1.0f;
    const float dx = pivot[0] - app.camera.position.x;
    const float dy = pivot[1] - app.camera.position.y;
    const float dz = pivot[2] - app.camera.position.z;
    const float dist = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3f);
    const float fov = app.camera.fovDeg * 3.14159265f / 180.0f;
    const float worldPerPx = (2.0f * dist * std::tan(fov * 0.5f)) / static_cast<float>(vp.extent.height);
 constexpr float kTargetPx = 120.0f; // 目标屏幕轴长（像素；用户 "轴长和箭头变大2倍"：60→120）
    return kTargetPx * worldPerPx;
}
int PickGizmoAxisAt(App& app, float mx, float my) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return -1;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
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
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）
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
bool RayPlane(const float o[3], const float d[3], const float n[3], const float p[3], float out[3]) {
    const float denom = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = (n[0] * (p[0] - o[0]) + n[1] * (p[1] - o[1]) + n[2] * (p[2] - o[2])) / denom;
    out[0] = o[0] + d[0] * t;
    out[1] = o[1] + d[1] * t;
    out[2] = o[2] + d[2] * t;
    return true;
}
bool HitGizmoRingAt(App& app, float mx, float my) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return false;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
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
void StartFreeDrag(App& app, HWND hwnd, float mx, float my) {
    SceneObject& so = app.scene.objects[app.scene.selectedObject];
    app.gizmo.gizmoDragMode = 3;
    app.gizmo.gizmoDragging = true;
    app.gizmo.gizmoStartTx = so.tx; app.gizmo.gizmoStartTy = so.ty; app.gizmo.gizmoStartTz = so.tz;
    GizmoPivot(so, app.gizmo.gizmoPivot);
 // 记录初始交点：移动 = 每帧交点增量
    app.gizmo.gizmoLastHitValid = false;
    float o[3], d[3];
    if (BuildViewRay(app, mx, my, o, d)) {
        float view[16];
        app.camera.ViewMatrix(view);
 const float n[3] = {-view[2], -view[6], -view[10]}; // 相机前向 = 平面法线（视口平面）
        if (RayPlane(o, d, n, app.gizmo.gizmoPivot, app.gizmo.gizmoLastHit)) app.gizmo.gizmoLastHitValid = true;
    }
 app.scene.mouseDragged = true; // 按下即视为拖拽：松开时不重新拾取
    SetCapture(hwnd);
}
int PickRotateGizmoAt(App& app, float mx, float my) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return -1;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return -1;
    const float len = GizmoAxisLen(app, p, vp);
    const float r = len * 0.78f;
 // 世界轴环基（gizmo 固定不随物体旋转）：与 DrawRotateGizmo 一致
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
 float hitDist = 10.0f * 10.0f; // 旋转模式缩小轴（环）检测范围 16→10px
 for (int i = 0; i < 3; ++i) { // 三环采样：返回距离最近者
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
 for (int i = 0; i < 3; ++i) { // 手柄球
        const float* u = basis[i][0];
        const float* w = basis[i][1];
        for (int q = 0; q < 4; ++q) {
            const float a = q * 3.14159265f * 0.5f;
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = p[k] + r * (std::cos(a) * u[k] + std::sin(a) * w[k]);
            float sx, sy;
            if (ProjectToViewport(mvp, gvp, c[0], c[1], c[2], sx, sy)) {
                const float ddx = mx - sx, ddy = my - sy;
 if (ddx * ddx + ddy * ddy < 12.0f * 12.0f) return i; // 手柄球检测 18→12px
            }
        }
    }
 // 中心轴点命中（放大后可点击）→ 返回 -2（trackball 自由旋转）
    {
        float sx, sy;
        if (ProjectToViewport(mvp, gvp, p[0], p[1], p[2], sx, sy)) {
            const float ddx = mx - sx, ddy = my - sy;
            if (ddx * ddx + ddy * ddy < 24.0f * 24.0f) return -2;
        }
    }
    return -1;
}
int PickScaleGizmoAt(App& app, float mx, float my) {
    if (app.scene.selectedObject < 0 || app.scene.selectedObject >= static_cast<int>(app.scene.objects.size())) return -1;
    const SceneObject& o = app.scene.objects[app.scene.selectedObject];
    float p[3];
    GizmoPivot(o, p);
    const Layout lay = ComputeLayout(app);
    const VkRect2D& vp = lay.viewport;
    if (vp.extent.width <= 0 || vp.extent.height <= 0) return -1;
    const float len = GizmoAxisLen(app, p, vp) * 0.72f;
 const float dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // 世界轴（gizmo 固定不随物体旋转）
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
 if (ddx * ddx + ddy * ddy < 18.0f * 18.0f) return 3; // 中心方块（等比）
        }
    }
    for (int i = 0; i < 3; ++i) {
        const float tip[3] = {p[0] + dirs[i][0] * len, p[1] + dirs[i][1] * len, p[2] + dirs[i][2] * len};
        float best = 1e9f;
 // 仅采样轴端外段 [0.55,1.0]（小立方体把手所在），与缩放外观对齐
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
void BuildObjectWireframe(SceneObject& o) {
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
 // 特征边判定已移至 BuildFeatureVerts（延迟构建）；此处仅生成全部边线框
    for (const auto& kv : adj) {
        const auto& key = kv.first;
        const uint32_t a = key.first, b = key.second;
 o.wireVerts.push_back(WirePos{{o.solidVerts[a].pos[0], o.solidVerts[a].pos[1], o.solidVerts[a].pos[2]}}); // 仅坐标
        o.wireVerts.push_back(WirePos{{o.solidVerts[b].pos[0], o.solidVerts[b].pos[1], o.solidVerts[b].pos[2]}});
        o.wireIndices.push_back(static_cast<uint32_t>(o.wireVerts.size()) - 2);
        o.wireIndices.push_back(static_cast<uint32_t>(o.wireVerts.size()) - 1);
 // featureVerts 已延迟——此处不再生成（见 BuildFeatureVerts）
    }
}
void BuildFeatureVerts(SceneObject& o) {
    o.featureVerts.clear();
    const auto& si = o.solidIndices;
    const auto& sv = o.solidVerts;
    if (si.size() < 3 || sv.empty()) return;
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> adj;
    for (size_t k = 0; k + 2 < si.size(); k += 3) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = si[k + e], b = si[k + (e + 1) % 3];
            if (a >= sv.size() || b >= sv.size()) continue;
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto& list = adj[key];
            if (list.size() < 2) list.push_back(static_cast<uint32_t>(k / 3));
        }
    }
 constexpr float kCreaseCos = 0.866f; // cos(30°)——法线夹角 > 30° 视为棱边
    for (const auto& kv : adj) {
        const auto& key = kv.first;
        const auto& tris = kv.second;
        bool feature = false;
        if (tris.size() == 1) {
 feature = true; // 开放边界边（border，只属于一个面）
        } else {
            const VertexSolid& v0 = sv[si[tris[0] * 3]];
            const VertexSolid& v1 = sv[si[tris[1] * 3]];
            const float dot = v0.normal[0] * v1.normal[0] +
                              v0.normal[1] * v1.normal[1] +
                              v0.normal[2] * v1.normal[2];
 if (dot < kCreaseCos) feature = true; // 法线夹角大 → 棱边
        }
        if (feature) {
            o.featureVerts.push_back(sv[key.first]);
            o.featureVerts.push_back(sv[key.second]);
        }
    }
}
void BuildSelSilhouette(const SceneObject& o, App& app) {
    app.scene.selSilA.clear(); app.scene.selSilB.clear(); app.scene.selSilN0.clear(); app.scene.selSilN1.clear();
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
    app.scene.selSilA.reserve(adj.size());
    app.scene.selSilB.reserve(adj.size());
    app.scene.selSilN0.reserve(adj.size() * 3);
    app.scene.selSilN1.reserve(adj.size() * 3);
    for (const auto& kv : adj) {
        const auto& tris = kv.second;
        if (tris.empty()) continue;
        app.scene.selSilA.push_back(kv.first.first);
        app.scene.selSilB.push_back(kv.first.second);
        float n0[3], n1[3];
        triNormal(tris[0], n0);
        if (tris.size() >= 2) triNormal(tris[1], n1);
        else { n1[0] = n1[1] = n1[2] = 0.0f; }
        for (int i = 0; i < 3; ++i) { app.scene.selSilN0.push_back(n0[i]); app.scene.selSilN1.push_back(n1[i]); }
    }
}

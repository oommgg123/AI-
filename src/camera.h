// ---------------------------------------------------------------------------
//   2026-08-19 从 main.cpp 抽离（用户要求"单独创建关于摄像机的 cpp 文件"），
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

enum class CameraPreset {
    Perspective,
    Top,
    Front,
    Right,
};

struct Camera {
    Vec3 position{8.0f, 6.0f, 8.0f};   // Round270：默认缩放更远（原 {2,1.6,2}→dist≈3.25；现 dist≈12.8）
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fovDeg = 45.0f;
    float nearPlane = 0.05f;
    float farPlane = 10000.0f;

    bool orbiting = false;
    bool panning = false;
    float lastX = 0.0f, lastY = 0.0f;

    // 轨道旋转状态（**累积变量，不每帧 atan2/asin 重算**——避免极点数值噪声）：
    float yaw   = std::atan2(8.0f, 8.0f);
    float pitch = std::asin(6.0f / 12.8062f);   // dist = sqrt(8²+6²+8²) ≈ 12.806

    float pendingYaw = 0.0f;
    float pendingPitch = 0.0f;
    float targetDist = -1.0f;

    // 摄像机阻尼（用户 168 轮设置项）：0~1，默认 0.05（低阻尼 → 跟手快）。
    // damping 越大 → 旋转/推拉越平滑缓慢（高阻尼）；越小 → 越跟手快速（低阻尼）。
    float damping = 0.05f;

    // 鼠标滑动灵敏度（用户 2026-08-20 设置项）：orbit/pan 输入增益，默认 1.0（由设置项映射）
    float orbitSensitivity = 1.0f;

    void Orbit(float dx, float dy) {
        constexpr float kSpeed = 0.006f;
        const float s = kSpeed * orbitSensitivity;
        pendingYaw   -= dx * s;
        pendingPitch += dy * s;
    }

    void Pan(float dx, float dy, float viewportH) {
        float view[16];
        ViewMatrix(view);
        const Vec3 rightV{view[0], view[4], view[8]};
        const Vec3 upV{view[1], view[5], view[9]};
        const Vec3 r{position.x - target.x, position.y - target.y, position.z - target.z};
        const float dist = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        const float scale = viewportH > 0.0f
            ? 2.0f * dist * std::tan(fovDeg * 0.5f * 3.14159265358979f / 180.0f) / viewportH
            : 0.01f;
        const Vec3 offset = (rightV * (-dx * scale) + upV * (dy * scale)) * orbitSensitivity;
        position = position + offset;
        target = target + offset;
    }

    void Zoom(float wheelDelta) {
        constexpr float kMinDist = 0.05f;
        constexpr float kMaxDist = 1500.0f;   // Round257：缩放最大值 10000 → 1500（用户要求）
        if (targetDist < 0.0f) {
            const Vec3 r{position.x - target.x, position.y - target.y, position.z - target.z};
            targetDist = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        }
        // 每格 -4% 而非 -5%（用户 147 轮：斜视角 Zoom 跨多 level → 密度突变"不一致"；
        // 4%/格更温和，滚几格不易跨 level 边界，Zoom 体验更线性）
        targetDist = std::clamp(targetDist * std::pow(0.96f, wheelDelta), kMinDist, kMaxDist);
    }

    void UpdateSmooth() {
        constexpr float kPi = 3.14159265358979f;
        // 阻尼映射（用户 168 轮设置项）：damping 大 → 每帧步长小（平滑慢）；
        // damping 小 → 步长大（跟手快）。范围：旋转 0.02~0.27/帧，缩放 0.05~0.60
        const float d = std::clamp(damping, 0.0f, 1.0f);
        const float kMaxRotStep = 0.02f + (1.0f - d) * 0.25f;
        const float kZoomLerp   = 0.05f + (1.0f - d) * 0.55f;
        const float dyaw = std::clamp(pendingYaw, -kMaxRotStep, kMaxRotStep);
        const float dpitch = std::clamp(pendingPitch, -kMaxRotStep, kMaxRotStep);
        pendingYaw   -= dyaw;
        pendingPitch -= dpitch;
        yaw   += dyaw;
        pitch += dpitch;
        if (pitch > kPi)       pitch -= 2.0f * kPi;
        else if (pitch < -kPi) pitch += 2.0f * kPi;
        if (yaw > kPi)       yaw -= 2.0f * kPi;
        else if (yaw < -kPi) yaw += 2.0f * kPi;
        const Vec3 r{position.x - target.x, position.y - target.y, position.z - target.z};
        const float dist = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        if (dist < 1e-6f) return;
        if (targetDist < 0.0f) targetDist = dist;
        const float newDist = dist + (targetDist - dist) * kZoomLerp;
        const float cp = std::cos(pitch);
        position = {target.x + newDist * cp * std::sin(yaw),
                    target.y + newDist * std::sin(pitch),
                    target.z + newDist * cp * std::cos(yaw)};
        up = {0.0f, std::cos(pitch), 0.0f};
    }

    void ViewMatrix(float out[16]) const {
        const auto norm = [](Vec3 v) {
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            // 0 距离保护（用户 182 轮 AMD 崩溃 0xC0000094：0/0=NaN → shader normalize → amdvk64 整除崩溃）
            if (l < 1e-6f) return Vec3{0.0f, 0.0f, -1.0f};
            return Vec3{v.x / l, v.y / l, v.z / l};
        };
        const auto cross = [](Vec3 a, Vec3 b) {
            return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        };
        const auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const auto sub = [](Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; };

        const Vec3 f = norm(sub(target, position));
        // turntable 固定右向量：只依赖 yaw（水平方向），与 pitch 无关（用户 162 轮）——
        // 旧代码 s = cross(f, up) 在 pitch 过 ±90° 时 up 变号 → s 翻转 180°（画面左右镜像），
        // 表现为"转到底部被限位、不能继续旋转 360°+"；固定 s 后任何角度连续、无退化
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const Vec3 s{cy, 0.0f, -sy};
        const Vec3 u = norm(cross(s, f));
        out[0] = s.x;  out[4] = s.y;  out[8]  = s.z;  out[12] = -dot(s, position);
        out[1] = u.x;  out[5] = u.y;  out[9]  = u.z;  out[13] = -dot(u, position);
        out[2] = -f.x; out[6] = -f.y; out[10] = -f.z; out[14] =  dot(f, position);
        out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;
    }

    void ProjectionMatrix(float aspect, float out[16]) const {
        const float f = 1.0f / std::tan(fovDeg * 0.5f * 3.14159265358979f / 180.0f);
        out[0] = f / aspect; out[4] = 0.0f; out[8]  = 0.0f; out[12] = 0.0f;
        out[1] = 0.0f;      out[5] = -f;   out[9]  = 0.0f; out[13] = 0.0f;
        out[2] = 0.0f;      out[6] = 0.0f; out[10] = farPlane / (nearPlane - farPlane);
        out[14] = nearPlane * farPlane / (nearPlane - farPlane);
        out[11] = -1.0f;    out[3] = 0.0f; out[7]  = 0.0f;
        out[15] = 0.0f;
    }

    void SetPreset(CameraPreset p);
};

// ---- 摄像机截图功能（保留接口，camera.cpp 内实现；可直接使用）----

// RGBA 像素 → PNG 文件（WIC 编码；路径 UTF-16 中文安全）
bool SaveRgbaPng(const wchar_t* path, const uint8_t* rgba, int width, int height);

bool SaveWindowShotPng(void* hwnd, const wchar_t* path);

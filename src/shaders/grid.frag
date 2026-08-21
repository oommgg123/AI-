#version 450

//   - **无圆形隐藏区**（用户要求"网格正常穿过立方体，而不是消失"）——网格
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 vp;
    mat4 invVP;
    vec4 lodBlend;
    float gridRadius;
    float smallFade;   // 小网格（细分 + 细主格）渐隐半径，动态 150~500
} pc;

const vec3 kGridColor    = vec3(0.30, 0.30, 0.34);  // 主格线：调亮（用户要求"亮一点"）
const vec3 kGridSubColor = vec3(0.22, 0.22, 0.26);  // 细分线：调亮（用户要求"亮一点"）

float lineDist(float g) { return abs(fract(g - 0.5) - 0.5); }

float lineMask(float d, float w) { return 1.0 - smoothstep(0.0, w * 2.0, d); }

float gridLines(vec2 g, vec2 w, float width) {
    return max(lineMask(lineDist(g.x), width * w.x), lineMask(lineDist(g.y), width * w.y));
}

void main() {
    vec4 q0 = pc.invVP * vec4(vNdc, 0.0, 1.0);
    vec4 q1 = pc.invVP * vec4(vNdc, 1.0, 1.0);
    vec3 ro = q0.xyz / q0.w;
    vec3 rd = q1.xyz / q1.w - ro;
    if (length(rd) < 1e-9) discard;

    if (abs(rd.y) < 1e-6) discard;
    float t = -ro.y / rd.y;
    if (t <= 0.0) discard;
    vec3 hit = ro + rd * t;

    vec4 clip = pc.vp * vec4(hit, 1.0);
    gl_FragDepth = clip.z / clip.w;

    // ===== 远处渐隐基准：摄像机位置垂直投影 (camX, camZ) =====
    vec2 camPosXZ = pc.lodBlend.zw;
    float hDist = length(hit.xz - camPosXZ);

    // ===== 固定世界间距层级（绝对间距制）=====
    // 线固定在世界坐标（0.125~64 米 10 层），**缩放时网格相对屏幕放大/缩小，与物体同步**
    // （用户 149 轮"改回来"——上一版 level≈dist 屏幕密度恒定，缩放只有物体动、网格不动）；
    // 层固定 → 无 level 跳变 → 零抖动；pitch（上下旋转）不影响
    // 每层渐隐半径 = 间距 × (smallFade/4)：4 米层 = smallFade（对齐用户"小网格 150~500"）
    float camDist  = pc.lodBlend.y;   // 相机到 target 距离（C++ push[33]）
    float smallFade = pc.smallFade;
    float subTotal  = 0.0f;   // 细分（0.125~0.5 米）：浅色
    float mainTotal = 0.0f;   // 主格（1 米+）：深色
    for (int k = 0; k < 10; ++k) {
        float d = exp2(float(k) - 3.0);   // 0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64 米
        // **亚线性渐隐半径**（用户 154 轮）：r = smallFade × (d/4)^0.4 —— 4 米层精确 = smallFade，
        // 大层（8 米+）显著小于线性值 → 远处大网格早点淡出不密集
        float r = smallFade * pow(d * 0.25, 0.4);
        // 8 米以上大层再打 6.5 折（拉远时画面内只剩粗层，不密集）
        if (k >= 6) r *= 0.65f;
        // 粗层（32/64 米，线宽 1.2）渐隐半径扩大 2 倍 + 显示距离再 +100（用户 161/164 轮）
        if (k >= 8) { r *= 2.0f; r += 100.0f; }
        vec2 gg = hit.xz / d;
        vec2 ww = min(max(fwidth(gg), vec2(1e-4)), vec2(2.0));
        // **fwidth 密度渐隐**（用户 163 轮激进阈值）：屏幕线距 < 40px 全亮、< 20px 完全消失
        // → Zoom out 时 0.5/1/2 米细层（屏幕间距 ~5~15px）全部淡出，画面内只剩 4/8/16 米粗层 → 稀疏
        // → 近处（线距 > 40px）细层仍清晰可见，不影响放大观察
        float densityFade = 1.0 - smoothstep(0.02, 0.05, max(ww.x, ww.y));
        float w = gridLines(gg, ww, (k < 3) ? 0.6 : (k < 8 ? 0.9 : 1.2)) * densityFade;
        // **放大门控逐层错开**（用户 153 轮）：最细三层各自独立阈值，放大时**逐层出现有间隔**
        // 0.5 米层 dist<3.2 出现 → 0.25 米层 <1.8 → 0.125 米层 <1.0
        if (k < 3) {
            const float g0[3] = {0.6f, 1.2f, 2.4f};
            const float g1[3] = {1.0f, 1.8f, 3.2f};
            w *= (1.0 - smoothstep(g0[k], g1[k], camDist));
        }
        // 渐隐带宽 0.85r~r（窄 → 变换快，用户 154 轮"大网格变换更快"）
        float fade = 1.0 - smoothstep(r * 0.85, r, hDist);
        if (k < 3) subTotal += w * fade;
        else       mainTotal += w * fade;
    }

    // ===== 远处渐隐（2D 圆形范围，基准点 = 摄像机位置投影）=====
    //     不会因为射线距离 t>25 而整片消失（旧 bug：拉远 → t 全 >25 → 全消失 → 缩回抽动）
    float gridRadius = pc.gridRadius;
    float horizonFade = 1.0 - smoothstep(gridRadius * 0.6, gridRadius, hDist);

    float m = max(mainTotal, subTotal) * horizonFade;
    if (m < 0.002) discard;

    vec3 gridColor = (mainTotal >= subTotal) ? kGridColor : kGridSubColor;
    outColor = vec4(gridColor, m);
}

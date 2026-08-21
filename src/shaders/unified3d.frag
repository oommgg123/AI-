#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vWorldXZ;
layout(location = 0) out vec4 outColor;

// 使用标量 float 以匹配 C++ Push3D 的字节布局（避免 vec2 的 8 字节对齐错位）
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float mode;
    float gridRadius;
    float modelRadius;
    float camXZx;
    float camXZz;
    float objXZx;
    float objXZz;
    float hasColor;   // 0=无颜色（紧凑模式，用常量色）
    float fadeDisable; // 1=禁用远处渐隐（MC 模型要完全不透明）
} pc;

// 无颜色模式（24B 紧凑顶点，用户 159 轮颜色位深切换）的常量色
const vec4 kWireColor = vec4(0.95, 0.95, 0.98, 1.0);
const vec4 kSolidColor = vec4(0.72, 0.72, 0.74, 1.0);

void main() {
    if (pc.mode < 0.5) {
        if (pc.hasColor > 1.5) outColor = vec4(vec3(vColor.x), 1.0);
        else if (pc.hasColor > 0.5) outColor = vColor;
        else outColor = kWireColor;
        return;
    }

    // flat 几何法线（ImportObj 已按面法线去重生成顶点，vNormal = 每个面的真实朝向）
    // 双面渲染：背面（gl_FrontFacing=false）取反使法线朝观察者
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;

    // Blender solid 双面光照：半球光（上亮下暗基础）+ 主光 + 补光
    // 光照基于**几何法线**（不随观察方向变）——相机过顶时顶部面仍亮，无"法线光照颠倒"
    float hemi = 0.5 + 0.5 * clamp(n.y, -1.0, 1.0);
    vec3 L1 = normalize(vec3( 0.45,  0.80,  0.40));  // 主光（前上）
    vec3 L2 = normalize(vec3(-0.55,  0.25, -0.45));  // 补光（后上）
    float light = 0.42 + 0.28 * hemi;
    light += 0.42 * max(dot(n, L1), 0.0);
    light += 0.18 * max(dot(n, L2), 0.0);
    // 颜色来源（用户 159 轮颜色位深）：hasColor 0=常量色(无颜色模式)、1=vColor、2=1bit 灰度广播
    vec4 base;
    if (pc.hasColor > 1.5) {
        base = vec4(vec3(vColor.x), 1.0);      // 1bit：单字节灰度 → RGB 相等
    } else if (pc.hasColor > 0.5) {
        base = vColor;
    } else {
        base = kSolidColor;
    }
    vec3 col = base.rgb * light;
    float alpha = base.a;

    // 远处渐隐：按**顶点**计算（超大模型远处顶点先消失、近处顶点保留）
    // 基准点 = camXZ（摄像机位置 XZ 投影，C++ 只在 Pan 平移时更新）——
    // 跟随摄像机位置（Pan 时），但 Orbit/Zoom 不改变渐隐状态（用户 135/140 轮要求）
    vec2 camXZ = vec2(pc.camXZx, pc.camXZz);
    float hDist = length(vWorldXZ - camXZ);
    // fadeEnd 自适应：至少覆盖"相机到物体中心的距离 + 模型半径"，保证大模型
    // fit 后整体不被硬裁（旧固定 fadeEnd=gridRadius 时，相机距物体 > gridRadius → 全裁掉 → 黑屏）
    float distToObj = length(camXZ - vec2(pc.objXZx, pc.objXZz));
    float fadeEnd   = max(pc.gridRadius, distToObj + pc.modelRadius);
    float fadeStart = max(fadeEnd - pc.modelRadius, 0.0);
    // 远处 +5 距离之外的顶点**直接不渲染**（硬裁剪，不进入渐隐）
    if (hDist > fadeEnd + 5.0) discard;
    float horizonFade = 1.0 - smoothstep(fadeStart, fadeEnd, hDist);
    // MC 模型禁用渐隐（fadeDisable=1）→ 完全不透明，无半透明淡出
    if (pc.fadeDisable < 0.5) alpha *= horizonFade;

    if (alpha < 0.002) discard;
    outColor = vec4(col, alpha);
}

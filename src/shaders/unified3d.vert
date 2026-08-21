#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec2 vWorldXZ;

// 注意：使用标量 float 而非 vec2，避免 GLSL push_constant 的 std140 8 字节对齐
// 导致 camXZ/objXZ 偏移错位（SPIR-V 会把 vec2 对齐到 80/88，而 C++ Push3D 在 76/84）
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float mode;
    float gridRadius;
    float modelRadius;
    float camXZx;
    float camXZz;
    float objXZx;
    float objXZz;
    float hasColor;   // 0=无颜色（紧凑模式，shader 用常量色）
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vNormal = inNormal;
    vColor = inColor;
    vWorldXZ = inPosition.xz + vec2(pc.objXZx, pc.objXZz);
}

#version 450

layout(binding = 0) uniform sampler2D maskTex;

layout(push_constant) uniform PushConstants {
    vec2 invScreen;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// Round370：选中物体屏幕投影描边——对 mask（选中物体白色区域）做 Sobel 边缘检测，
// 边缘处输出黄色（alpha=1），非边缘输出透明（alpha=0，blend 保留底下画面）
void main() {
    float e[3][3];
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            e[x + 1][y + 1] = texture(maskTex, vUv + vec2(float(x), float(y)) * pc.invScreen).r;
    float gx = (e[2][0] + 2.0 * e[2][1] + e[2][2]) - (e[0][0] + 2.0 * e[0][1] + e[0][2]);
    float gy = (e[0][2] + 2.0 * e[1][2] + e[2][2]) - (e[0][0] + 2.0 * e[1][0] + e[2][0]);
    float grad = sqrt(gx * gx + gy * gy);
    if (grad > 0.12)
        outColor = vec4(1.0, 0.84, 0.1, 1.0);   // 黄
    else
        outColor = vec4(0.0);
}

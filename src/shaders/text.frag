#version 450

layout(binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    vec4 rect;
    vec4 color;
    float mono;   // 0=直通纹理真实颜色（软件图标专用特殊渲染通道）；1=纯白渲染（按钮图标默认）
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = (gl_FragCoord.xy - pc.rect.xy) / pc.rect.zw;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    vec4 texel = texture(tex, uv);
    if (texel.a < 0.01) discard;
    if (pc.mono > 0.5) {
        // 纯白渲染通道：用纹理 alpha 作蒙版、输出纯白；将来彩色按钮图标改走 color 通道（white=false）即可
        outColor = vec4(1.0, 1.0, 1.0, texel.a);
    } else {
        // 特殊渲染通道（软件图标 awa logo）：直通纹理真实颜色
        outColor = texel;
    }
}

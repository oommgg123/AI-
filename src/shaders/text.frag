#version 450

layout(binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    vec4 rect;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = (gl_FragCoord.xy - pc.rect.xy) / pc.rect.zw;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    float a = texture(tex, uv).a;
    if (a < 0.01) discard;
    // Round295：已取消 gamma 编码（恢复直出颜色）
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}

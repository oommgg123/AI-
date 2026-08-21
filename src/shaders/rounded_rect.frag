#version 450


layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4  rect;
    vec4  rect2;
    vec4  fillColor;
    vec4  borderColor;
    float cornerRadius;
    float borderWidth;
    float mode;
    float lineHalfWidth;
} pc;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    // ---- 线段模式（坐标轴等） ----
    if (pc.mode > 0.5) {
        float d = sdSegment(gl_FragCoord.xy, pc.rect.xy, pc.rect2.xy);
        if (d > pc.lineHalfWidth) discard;
        // Round295：已取消 gamma 编码（恢复直出颜色）
        outColor = pc.fillColor;
        return;
    }

    // ---- 矩形模式（圆角 + 渐变边框） ----
    vec2  halfSize = 0.5 * pc.rect.zw;
    vec2  center   = pc.rect.xy + halfSize;
    float radius   = min(pc.cornerRadius, min(halfSize.x, halfSize.y));

    vec2 p = gl_FragCoord.xy - center;
    float dOuter = sdRoundedBox(p, halfSize, radius);
    if (dOuter > 0.0) discard;

    if (pc.borderWidth <= 0.0) {
        // Round295：已取消 gamma 编码（恢复直出颜色）
        outColor = pc.fillColor;
        return;
    }

    float dInner = sdRoundedBox(p, halfSize - vec2(pc.borderWidth),
                                max(radius - pc.borderWidth, 0.0));
    float s = smoothstep(0.0, pc.borderWidth, dInner);
    vec3 rgb = mix(pc.borderColor.rgb, pc.fillColor.rgb, 1.0 - s);
    // Round295：已取消 gamma 编码（恢复直出颜色）
    outColor = vec4(rgb, 1.0);
}

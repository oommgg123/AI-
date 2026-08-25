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
        // 方头线段（无圆头）：sdSegment 无限延伸 + AABB 裁剪实现方头端点
        vec2 a = pc.rect.xy;
        vec2 b = pc.rect2.xy;
        vec2 pa = gl_FragCoord.xy - a;
        vec2 ba = b - a;
        float h = dot(pa, ba) / dot(ba, ba);   // 不 clamp（无限延伸直线）
        float d = length(pa - ba * h);
        if (d > pc.lineHalfWidth) discard;
        // 端点 AABB 裁剪：方头（去掉 sdSegment 的圆头圆弧）
        vec2 mn = min(a, b) - vec2(pc.lineHalfWidth);
        vec2 mx = max(a, b) + vec2(pc.lineHalfWidth);
        if (gl_FragCoord.x < mn.x || gl_FragCoord.x > mx.x ||
            gl_FragCoord.y < mn.y || gl_FragCoord.y > mx.y) discard;
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
    // Roundxxx：alpha 保留 fillColor.a（透明 fill + 边框 = "仅描边"模式，配合 alpha 混合管线不覆盖内部）
    outColor = vec4(rgb, pc.fillColor.a);
}

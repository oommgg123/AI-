#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 pointA;
    vec4 pointB;
    vec4 params;
    vec4 color;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    float t = float((gl_VertexIndex >> 1) & 1);
    float s = float(gl_VertexIndex & 1) * 2.0 - 1.0;

    vec4 a = pc.mvp * vec4(pc.pointA.xyz, 1.0);
    vec4 b = pc.mvp * vec4(pc.pointB.xyz, 1.0);

    // 任一端点在相机后（w<=0）→ 整段移出裁剪空间（丢弃，避免投影翻转）
    if (a.w <= 0.0 || b.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vColor = pc.color;
        return;
    }

    vec2 ndcA = a.xy / a.w;
    vec2 ndcB = b.xy / b.w;
    vec2 dirNdc = ndcB - ndcA;

    float vw = pc.params.y;
    float vh = pc.params.z;

    vec2 dirPx = dirNdc * vec2(0.5 * vw, 0.5 * vh);
    float lenPx = length(dirPx);
    vec2 perpPx = vec2(-dirPx.y, dirPx.x) / max(lenPx, 1e-6);
    vec2 offsetPx = perpPx * pc.params.x;
    vec2 offsetNdc = vec2(offsetPx.x * 2.0 / vw, offsetPx.y * 2.0 / vh);

    vec2 ndcPos = mix(ndcA, ndcB, t) + offsetNdc * s;

    vec4 pos = mix(a, b, t);
    pos.xy = ndcPos * pos.w;
    gl_Position = pos;
    vColor = pc.color;
}

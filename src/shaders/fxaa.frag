#version 450

layout(binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    vec2 invScreen;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 texel = pc.invScreen;

    vec3 rgbNW = texture(tex, vUv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture(tex, vUv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture(tex, vUv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture(tex, vUv + vec2( 1.0,  1.0) * texel).rgb;
    vec3 rgbM  = texture(tex, vUv).rgb;

    const vec3 lumaK = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, lumaK);
    float lumaNE = dot(rgbNE, lumaK);
    float lumaSW = dot(rgbSW, lumaK);
    float lumaSE = dot(rgbSE, lumaK);
    float lumaM  = dot(rgbM,  lumaK);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0312);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(8.0), max(vec2(-8.0), dir * rcpDirMin)) * texel;

    vec3 rgbA = 0.5 * (texture(tex, vUv + dir * (1.0 / 3.0 - 0.5)).rgb +
                        texture(tex, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(tex, vUv + dir * -0.5).rgb +
                                      texture(tex, vUv + dir *  0.5).rgb);
    float lumaB = dot(rgbB, lumaK);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        outColor = vec4(rgbA, 1.0);
    } else {
        outColor = vec4(rgbB, 1.0);
    }
}

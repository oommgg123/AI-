#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 L = normalize(vec3(0.4, 0.9, 0.6));
    float diff = max(dot(n, L), 0.0);
    float light = 0.45 + 0.65 * diff;
    outColor = vec4(vColor.rgb * light, vColor.a);
}

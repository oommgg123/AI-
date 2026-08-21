#version 450

layout(location = 0) in vec2 inPos;
layout(location = 0) out vec2 vUv;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    vUv = inPos * 0.5 + 0.5;
}

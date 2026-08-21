#version 450

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vColor.rgb, 1.0);   // Round271：线框/三向标/选中框一律不透明（强制 alpha=1）
}

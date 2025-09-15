#version 310 es
precision mediump float;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1) uniform sampler2D FontTexture;

void main() {
    float alpha = texture(FontTexture, vTexCoord).r;
    FragColor = vec4(vColor.rgb, vColor.a * alpha);

    //FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red color
}
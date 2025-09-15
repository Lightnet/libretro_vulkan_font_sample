#version 310 es
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 2) in vec4 Color;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 MVP;
};

void main() {
    gl_Position = MVP * vec4(Position, 0.0, 1.0);
    vColor = Color;
    vTexCoord = TexCoord;
}
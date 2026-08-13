#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;

layout(push_constant) uniform Camera {
    mat4 view_projection;
} camera;

layout(location = 0) out vec4 vertex_color;

void main()
{
    gl_Position = camera.view_projection * vec4(in_position, 0.0, 1.0);
    vertex_color = in_color;
}

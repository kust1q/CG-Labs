#version 450 core

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view_proj;
} scene;

layout(push_constant) uniform Marker {
    mat4 model;
    vec4 color;
} marker;

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = scene.view_proj * marker.model * vec4(in_position, 1.0);
    out_color = marker.color.rgb;
}

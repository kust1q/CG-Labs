#version 450 core

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view_proj;
    vec4 camera_position;
    vec4 ambient;
    vec4 dir_light_direction;
    vec4 dir_light_color;
    uint point_light_count;
    uint spot_light_count;
} scene;

layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normal_matrix;
    vec4 albedo;
    vec4 specular;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_world_normal;

void main() {
    const vec4 world_position = object.model * vec4(in_position, 1.0);

    out_world_position = world_position.xyz;
    out_world_normal = mat3(object.normal_matrix) * in_normal;

    gl_Position = scene.view_proj * world_position;
}

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

struct PointLight {
    vec4 position;
    vec4 color;
};

struct SpotLight {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 cutoff;
};

layout(std430, set = 0, binding = 1) readonly buffer PointLightBuffer {
    PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spot_lights[];
};

layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normal_matrix;
    vec4 albedo;
    vec4 specular;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;

layout(location = 0) out vec4 out_color;

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance) {
    const float n_dot_l = dot(N, L);
    if (n_dot_l <= 0.0) {
        return vec3(0.0);
    }

    const vec3 H = normalize(L + V);
    const float spec = pow(max(dot(N, H), 0.0), object.specular.w);

    const vec3 diffuse = object.albedo.rgb * n_dot_l;
    const vec3 specular = object.specular.rgb * spec;
    return (diffuse + specular) * radiance;
}

void main() {
    const vec3 N = normalize(in_world_normal);
    const vec3 V = normalize(scene.camera_position.xyz - in_world_position);

    vec3 color = scene.ambient.rgb * object.albedo.rgb;

    const float dir_length2 = dot(scene.dir_light_direction.xyz, scene.dir_light_direction.xyz);
    if (dir_length2 > 1e-8) {
        const vec3 L = -scene.dir_light_direction.xyz * inversesqrt(dir_length2);
        color += blinnPhong(N, V, L, scene.dir_light_color.rgb);
    }

    for (uint i = 0; i < scene.point_light_count; ++i) {
        const vec3 to_light = point_lights[i].position.xyz - in_world_position;
        const float distance2 = max(dot(to_light, to_light), 1e-4);
        const vec3 L = to_light * inversesqrt(distance2);

        color += blinnPhong(N, V, L, point_lights[i].color.rgb / distance2);
    }

    for (uint i = 0; i < scene.spot_light_count; ++i) {
        const SpotLight light = spot_lights[i];

        const vec3 to_light = light.position.xyz - in_world_position;
        const float distance2 = max(dot(to_light, to_light), 1e-4);
        const vec3 L = to_light * inversesqrt(distance2);

        const float cos_theta = dot(-L, normalize(light.direction.xyz));
        const float cos_inner = light.cutoff.x;
        const float cos_outer = light.cutoff.y;

        const float cone = cos_outer < cos_inner
            ? smoothstep(cos_outer, cos_inner, cos_theta)
            : step(cos_inner, cos_theta);

        if (cone > 0.0) {
            color += blinnPhong(N, V, L, light.color.rgb * cone / distance2);
        }
    }

    out_color = vec4(color, 1.0);
}

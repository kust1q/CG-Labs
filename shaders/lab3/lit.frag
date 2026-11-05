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

layout(set = 1, binding = 1) uniform sampler2D albedo_texture;

// Must match SamplingPushConstants in application.cpp (four 32-bit fields).
layout(push_constant) uniform Sampling {
    uint enabled;
    float time;
    float strength;
    float uv_scale;
} sampling;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

vec3 sampleAlbedo() {
    const vec3 base = texture(albedo_texture, in_uv).rgb;
    if (sampling.enabled == 0u) {
        return base * object.albedo.rgb;
    }

    const float t = sampling.time;
    const vec2 uv = in_uv * sampling.uv_scale;
    const vec2 distortion = 0.06 * vec2(
        sin(uv.y * 12.56637 + t * 1.4),
        cos(uv.x * 12.56637 - t * 1.1));
    const float angle = 0.35 * sin(t * 0.7);
    const mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));

    // Read the same image at two independently animated texture coordinates.
    const vec2 uv_a = uv + distortion + vec2(t * 0.08, -t * 0.04);
    const vec2 uv_b = rotation * (uv - 0.5) + 0.5 - distortion + vec2(-t * 0.05, t * 0.07);
    const vec3 a = texture(albedo_texture, uv_a).rgb;
    const vec3 b = texture(albedo_texture, uv_b).rgb;
    const float blend = 0.5 + 0.25 * sin(t + in_uv.y * 6.283185);
    const vec3 tint = 0.75 + 0.25 * sin(vec3(0.0, 2.094395, 4.188790) + t);
    const vec3 animated = mix(a, b, blend) * tint;
    return mix(base, animated, sampling.strength) * object.albedo.rgb;
}

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo) {
    const float n_dot_l = dot(N, L);
    if (n_dot_l <= 0.0) {
        return vec3(0.0);
    }

    const vec3 H = normalize(L + V);
    const float spec = pow(max(dot(N, H), 0.0), object.specular.w);

    const vec3 diffuse = albedo * n_dot_l;
    const vec3 specular = object.specular.rgb * spec;
    return (diffuse + specular) * radiance;
}

void main() {
    const vec3 N = normalize(in_world_normal);
    const vec3 V = normalize(scene.camera_position.xyz - in_world_position);

    const vec3 albedo = sampleAlbedo();
    vec3 color = scene.ambient.rgb * albedo;

    const float dir_length2 = dot(scene.dir_light_direction.xyz, scene.dir_light_direction.xyz);
    if (dir_length2 > 1e-8) {
        const vec3 L = -scene.dir_light_direction.xyz * inversesqrt(dir_length2);
        color += blinnPhong(N, V, L, scene.dir_light_color.rgb, albedo);
    }

    for (uint i = 0; i < scene.point_light_count; ++i) {
        const vec3 to_light = point_lights[i].position.xyz - in_world_position;
        const float distance2 = max(dot(to_light, to_light), 1e-4);
        const vec3 L = to_light * inversesqrt(distance2);

        color += blinnPhong(N, V, L, point_lights[i].color.rgb / distance2, albedo);
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
            color += blinnPhong(N, V, L, light.color.rgb * cone / distance2, albedo);
        }
    }

    out_color = vec4(color, 1.0);
}

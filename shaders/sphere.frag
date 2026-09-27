#version 450 core

layout(location = 0) in vec3 frag_color;    // тот же location, тот же тип, что в .vert
layout(location = 0) out vec4 out_color;      // цвет пикселя в color attachment

void main() {
    out_color = vec4(frag_color, 1.0);   // frag_color — это vec3, а нужен vec4. Какое значение альфы?
}

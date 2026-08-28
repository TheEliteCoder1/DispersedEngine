#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec2 vUV;
layout(set=1, binding=0) uniform VertUBO { mat4 mvp; mat4 model; };
void main() {
    vNormal = mat3(model) * inNormal;
    vUV = inUV;
    gl_Position = mvp * vec4(inPos, 1.0);
}
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;
layout(push_constant) uniform Push { mat4 mvp; } pushConstants;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;

void main() {
    vNormal = inNormal;
    vColor = inColor;
    vUV = inUV;
    gl_Position = pushConstants.mvp * vec4(inPos, 1.0);
}
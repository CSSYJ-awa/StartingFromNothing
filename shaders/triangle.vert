#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(push_constant) uniform Push { mat4 mvp; } pushConstants;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

void main() {
    vNormal = inNormal;
    vColor = inColor;
    gl_Position = pushConstants.mvp * vec4(inPos, 1.0);
}

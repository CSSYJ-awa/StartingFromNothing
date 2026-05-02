#version 450
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform Push { mat4 mvp; } pushConstants;

void main() {
    gl_Position = pushConstants.mvp * vec4(inPos, 1.0);
}

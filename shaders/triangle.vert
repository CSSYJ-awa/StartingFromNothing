#version 450
layout(push_constant) uniform Push { mat4 mvp; } pushConstants;

void main() {
    // a simple 3D triangle in model space
    const vec3 pos[3] = vec3[3](vec3(0.0, -0.5, 0.0), vec3(0.5, 0.5, 0.0), vec3(-0.5, 0.5, 0.0));
    gl_Position = pushConstants.mvp * vec4(pos[gl_VertexIndex], 1.0);
}

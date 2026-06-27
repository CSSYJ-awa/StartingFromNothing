#version 450
#extension GL_ARB_separate_shader_objects : enable

/**
 * vertex.glsl —— 顶点着色器
 *
 * 输入：
 *   - position: 顶点位置（世界坐标）
 *   - normal:   法线方向
 *   - color:    顶点颜色（RGBA）
 *   - ao:       环境光遮蔽因子
 *
 * 输出：
 *   - fragColor: 传递给片元着色器的颜色
 *   - fragNormal: 法线（用于光照计算）
 *
 * 变换：
 *   - 模型矩阵为单位矩阵（顶点已在世界坐标中）
 *   - 视图/投影矩阵由统一缓冲区提供
 */

// ---- 顶点输入 ----
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inAO;

// ---- 统一缓冲区 ----
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// ---- 片元着色器输入 ----
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragAO;
layout(location = 3) out vec3 fragWorldPos;

void main()
{
    // 顶点已在世界坐标中，模型矩阵 = 单位矩阵
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    // 传递到片元着色器
    fragColor    = inColor;
    fragNormal   = normalize(mat3(ubo.model) * inNormal);
    fragAO       = inAO;
    fragWorldPos = worldPos.xyz;
}

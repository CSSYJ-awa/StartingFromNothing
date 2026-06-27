#version 450
#extension GL_ARB_separate_shader_objects : enable

/**
 * fragment.glsl —— 片元着色器
 *
 * 实现基础光照模型：
 * - 环境光 (Ambient): 基础亮度，保证背光面可见
 * - 漫反射 (Diffuse): 基于法线与光源方向的点积
 * - 简单雾效: 远处物体淡出到天空色
 *
 * 光照方向为固定的顺光方向（模拟太阳）。
 *
 * 【远近分区差异化渲染说明】
 * 目前所有区域使用相同的着色器。
 * 实际优化中可以为远区使用简化版本的着色器（禁用光照、雾效等），
 * 通过在管线创建时切换不同的片元着色器实现。
 */

// ---- 片元着色器输入 ----
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragAO;
layout(location = 3) in vec3 fragWorldPos;

// ---- 输出 ----
layout(location = 0) out vec4 outColor;

void main()
{
    // ---- 光照参数 ----
    // 固定方向光（模拟太阳）
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    vec3 ambientColor = vec3(0.4, 0.5, 0.6);   // 天光色
    vec3 sunColor = vec3(1.0, 0.95, 0.85);      // 太阳光色

    // ---- 漫反射光照 ----
    float diff = max(dot(fragNormal, lightDir), 0.0);
    vec3 diffuse = diff * sunColor;

    // ---- 环境光 ----
    vec3 ambient = ambientColor * 0.4;

    // ---- AO 因子 ----
    float aoFactor = 0.6 + 0.4 * fragAO;

    // ---- 合并光照 ----
    vec3 lighting = (ambient + diffuse) * aoFactor;

    // ---- 应用颜色 ----
    vec3 finalColor = fragColor.rgb * lighting;

    // ---- 简单雾效（基于距离） ----
    float distance = length(fragWorldPos);
    float fogFactor = clamp((distance - 100.0) / 300.0, 0.0, 0.6);
    vec3 fogColor = vec3(0.5, 0.7, 1.0); // 天空色

    finalColor = mix(finalColor, fogColor, fogFactor);

    // ---- 输出 ----
    outColor = vec4(finalColor, fragColor.a);
}

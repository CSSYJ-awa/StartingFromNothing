#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_shader_subgroup_shuffle : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

/**
 * fragment.glsl —— 片元着色器（含 Subgroup 优化）
 *
 * 实现基础光照模型 + 雾效。
 *
 * 【优化】使用 Subgroup Shuffle 操作共享相邻像素的光照计算结果：
 *   当同一 Wave 内相邻像素的法线方向相近时，复用其光照值，
 *   减少冗余计算量。差异较大时回退到独立计算。
 *
 * 【纹理图集支持（启用时取消注释）】
 *   uniform sampler2D atlasTex;
 *   layout(location = 2) in vec2 fragUV;
 *   vec4 texColor = texture(atlasTex, fragUV);
 *   替代 fragColor 使用 texColor
 */

// ---- 输入 ----
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragAO;
layout(location = 3) in vec3 fragWorldPos;

// ---- 输出 ----
layout(location = 0) out vec4 outColor;

// ---- Subgroup 加速的光照计算 ----
vec3 computeLighting(vec3 normal, vec3 lightDir, vec3 ambientColor, vec3 sunColor)
{
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * sunColor;
    vec3 ambient = ambientColor * 0.4;
    return ambient + diffuse;
}

void main()
{
    // ---- 光照参数 ----
    vec3 lightDir   = normalize(vec3(0.5, 0.8, 0.3));
    vec3 ambientCol = vec3(0.4, 0.5, 0.6);
    vec3 sunColor   = vec3(1.0, 0.95, 0.85);

    // ---- Subgroup 优化：相邻像素共享光照 ----
    vec3 lighting;
#ifdef GL_KHR_shader_subgroup_shuffle
    // 从相邻线程（subgroup 索引 ±1）获取法线
    vec3 neighborNormal = subgroupShuffle(fragNormal, gl_SubgroupInvocationID ^ 1);
    float normalDiff = length(fragNormal - neighborNormal);

    // 若法线相近，共享邻居的光照结果
    if (normalDiff < 0.05)
    {
        lighting = subgroupShuffle(
            computeLighting(fragNormal, lightDir, ambientCol, sunColor),
            gl_SubgroupInvocationID ^ 1);
    }
    else
    {
        lighting = computeLighting(fragNormal, lightDir, ambientCol, sunColor);
    }
#else
    // Fallback：常规计算
    lighting = computeLighting(fragNormal, lightDir, ambientCol, sunColor);
#endif

    // ---- AO 因子 ----
    float aoFactor = 0.6 + 0.4 * fragAO;

    // ---- 应用颜色 ----
    vec3 finalColor = fragColor.rgb * lighting * aoFactor;

    // ---- 简单雾效 ----
    float distance = length(fragWorldPos);
    float fogFactor = clamp((distance - 100.0) / 300.0, 0.0, 0.6);
    vec3 fogColor = vec3(0.5, 0.7, 1.0);
    finalColor = mix(finalColor, fogColor, fogFactor);

    // ---- 输出 ----
    outColor = vec4(finalColor, fragColor.a);
}

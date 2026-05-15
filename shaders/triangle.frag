#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normalize(vNormal), lightDir), 0.0);
    vec3 color = vColor * (0.2 + 0.8 * diff);
    
    // 如果UV是(0,0)，表示是按钮，不使用纹理
    if (vUV.x < 0.001 && vUV.y < 0.001) {
        outColor = vec4(color, 1.0);
    } else {
        // 采样纹理（顶点数据已处理V坐标翻转）
        float texAlpha = texture(texSampler, vUV).r;
        // 文字使用白色
        outColor = vec4(1.0, 1.0, 1.0, texAlpha);
    }
}
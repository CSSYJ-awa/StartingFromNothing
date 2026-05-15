#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace ui {

class UIRenderer {
public:
    static UIRenderer& getInstance();
    
    // 初始化渲染器
    void init();
    
    // 渲染矩形
    void renderRect(float x, float y, float width, float height, 
                    const glm::vec3& color, float alpha = 1.0f);
    
    // 渲染带边框的矩形
    void renderRectWithBorder(float x, float y, float width, float height,
                              const glm::vec3& color, float alpha,
                              const glm::vec3& borderColor, float borderWidth);
    
    // 渲染九宫格（用于按钮等可缩放组件）
    void renderNinePatch(float x, float y, float width, float height,
                         const std::string& textureName);
    
    // 渲染文字
    void renderText(const std::string& text, float x, float y, float scale = 1.0f,
                    const glm::vec3& color = glm::vec3(1.0f));
    
    // 渲染带阴影的文字
    void renderTextWithShadow(const std::string& text, float x, float y, 
                              float scale = 1.0f, const glm::vec3& color = glm::vec3(1.0f));
    
    // 获取文字宽度
    float getTextWidth(const std::string& text, float scale = 1.0f);
    
    // 获取文字高度
    float getTextHeight(float scale = 1.0f);
    
    // 准备渲染（清空顶点缓冲区）
    void beginRender();
    
    // 提交渲染（绘制所有累积的顶点）
    void endRender();
    
    // 设置投影矩阵（正交投影）
    void setProjectionMatrix(const glm::mat4& proj);
    
private:
    UIRenderer() = default;
    ~UIRenderer() = default;
    
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;
    
    std::vector<float> m_vertices;
    glm::mat4 m_projectionMatrix;
};

} // namespace ui
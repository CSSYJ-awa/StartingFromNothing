#include "UIRenderer.h"
#include <cstdint>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 全局字体相关变量（来自 voxel_game.cpp）
extern unsigned char* g_fontAtlas;
extern int g_fontAtlasWidth;
extern int g_fontAtlasHeight;

struct FontGlyph {
    float u0, v0, u1, v1;
    float width, height;
    float advance;
    float bearingX;
    float bearingY;
};

extern std::unordered_map<unsigned int, FontGlyph> g_fontGlyphs;

// 顶点数据（位置、法线、颜色、UV）
struct UIVertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
    float u, v;
};

namespace ui {

UIRenderer& UIRenderer::getInstance() {
    static UIRenderer instance;
    return instance;
}

void UIRenderer::init() {
    // 设置默认正交投影矩阵（1920x1080 分辨率）
    m_projectionMatrix = glm::ortho(0.0f, 1920.0f, 1080.0f, 0.0f, -1.0f, 1.0f);
}

void UIRenderer::renderRect(float x, float y, float width, float height,
                            const glm::vec3& color, float alpha) {
    // 简单实现：使用纯色填充
    // 实际实现需要将顶点添加到 m_vertices
    (void)x; (void)y; (void)width; (void)height;
    (void)color; (void)alpha;
}

void UIRenderer::renderRectWithBorder(float x, float y, float width, float height,
                                      const glm::vec3& color, float alpha,
                                      const glm::vec3& borderColor, float borderWidth) {
    // 渲染背景
    renderRect(x, y, width, height, color, alpha);
    
    // 渲染边框
    renderRect(x, y, width, borderWidth, borderColor, 1.0f);
    renderRect(x, y + height - borderWidth, width, borderWidth, borderColor, 1.0f);
    renderRect(x, y, borderWidth, height, borderColor, 1.0f);
    renderRect(x + width - borderWidth, y, borderWidth, height, borderColor, 1.0f);
}

void UIRenderer::renderNinePatch(float x, float y, float width, float height,
                                 const std::string& textureName) {
    (void)x; (void)y; (void)width; (void)height;
    (void)textureName;
}

void UIRenderer::renderText(const std::string& text, float x, float y, float scale,
                            const glm::vec3& color) {
    float cursorX = x;
    float cursorY = y;
    
    for (char c : text) {
        unsigned int uc = static_cast<unsigned char>(c);
        auto it = g_fontGlyphs.find(uc);
        if (it == g_fontGlyphs.end()) continue;
        
        FontGlyph& glyph = it->second;
        
        if (glyph.width > 0) {
            // 计算字符位置
            float px = cursorX + glyph.bearingX * scale;
            float py = cursorY - (glyph.height - glyph.bearingY) * scale;
            float pw = glyph.width * scale;
            float ph = glyph.height * scale;
            
            // 添加四边形顶点（两个三角形）
            float v[] = {
                px, py + ph, 0.0f,    0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u0, glyph.v0,
                px + pw, py + ph, 0.0f,  0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u1, glyph.v0,
                px + pw, py, 0.0f,     0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u1, glyph.v1,
                px, py + ph, 0.0f,    0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u0, glyph.v0,
                px + pw, py, 0.0f,     0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u1, glyph.v1,
                px, py, 0.0f,        0.5f, 1.0f, 0.3f,    color.r, color.g, color.b,    glyph.u0, glyph.v1
            };
            
            m_vertices.insert(m_vertices.end(), v, v + sizeof(v) / sizeof(float));
        }
        
        cursorX += glyph.advance * scale;
    }
}

void UIRenderer::renderTextWithShadow(const std::string& text, float x, float y,
                                      float scale, const glm::vec3& color) {
    // 先渲染阴影
    renderText(text, x + 2, y + 2, scale, glm::vec3(0.0f, 0.0f, 0.0f));
    // 再渲染文字
    renderText(text, x, y, scale, color);
}

float UIRenderer::getTextWidth(const std::string& text, float scale) {
    float width = 0;
    for (char c : text) {
        unsigned int uc = static_cast<unsigned char>(c);
        auto it = g_fontGlyphs.find(uc);
        if (it != g_fontGlyphs.end()) {
            width += it->second.advance * scale;
        }
    }
    return width;
}

float UIRenderer::getTextHeight(float scale) {
    // 使用 'M' 字符作为参考高度
    auto it = g_fontGlyphs.find('M');
    if (it != g_fontGlyphs.end()) {
        return it->second.height * scale;
    }
    return 24.0f * scale; // 默认高度
}

void UIRenderer::beginRender() {
    m_vertices.clear();
}

void UIRenderer::endRender() {
    // 这里应该提交顶点到 GPU 渲染
    // 实际实现需要调用 Vulkan 绘制命令
}

void UIRenderer::setProjectionMatrix(const glm::mat4& proj) {
    m_projectionMatrix = proj;
}

} // namespace ui
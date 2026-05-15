#pragma once

#include "Widget.h"
#include <string>
#include <glm/glm.hpp>

namespace ui {

class Label : public Widget {
public:
    Label(float x, float y, const std::string& text);
    Label(float x, float y, float width, float height, const std::string& text);
    
    void render() override;
    
    // 设置文本
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }
    
    // 设置文本颜色
    void setTextColor(const glm::vec3& color) { m_textColor = color; }
    const glm::vec3& getTextColor() const { return m_textColor; }
    
    // 设置背景颜色（透明则不绘制背景）
    void setBackgroundColor(const glm::vec3& color, float alpha = 1.0f) { 
        m_backgroundColor = color; 
        m_backgroundAlpha = alpha; 
    }
    const glm::vec3& getBackgroundColor() const { return m_backgroundColor; }
    
private:
    std::string m_text;
    glm::vec3 m_textColor;
    glm::vec3 m_backgroundColor;
    float m_backgroundAlpha;
};

} // namespace ui
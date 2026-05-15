#include "Button.h"
#include "UIRenderer.h"

namespace ui {

Button::Button(float x, float y, float width, float height, const std::string& text)
    : Widget(x, y, width, height), m_text(text), m_state(State::NORMAL) {}

void Button::render() {
    if (!m_visible) return;
    
    UIRenderer& renderer = UIRenderer::getInstance();
    glm::vec3 bgColor;
    glm::vec3 textColor;
    
    switch (m_state) {
        case State::NORMAL:
            bgColor = glm::vec3(0.5f, 0.5f, 0.5f);
            textColor = glm::vec3(1.0f, 1.0f, 1.0f);
            break;
        case State::HOVERED:
            bgColor = glm::vec3(0.6f, 0.6f, 0.6f);
            textColor = glm::vec3(1.0f, 1.0f, 1.0f);
            break;
        case State::PRESSED:
            bgColor = glm::vec3(0.4f, 0.4f, 0.4f);
            textColor = glm::vec3(0.9f, 0.9f, 0.9f);
            break;
        case State::DISABLED:
            bgColor = glm::vec3(0.3f, 0.3f, 0.3f);
            textColor = glm::vec3(0.5f, 0.5f, 0.5f);
            break;
    }
    
    // 渲染按钮背景
    renderer.renderRect(m_x, m_y, m_width, m_height, bgColor, 1.0f);
    
    // 渲染边框
    renderer.renderRectWithBorder(m_x, m_y, m_width, m_height, 
                                  bgColor, 1.0f, 
                                  glm::vec3(0.8f, 0.8f, 0.8f), 2.0f);
    
    // 渲染文字（居中）
    float textWidth = renderer.getTextWidth(m_text);
    float textHeight = renderer.getTextHeight();
    float textX = m_x + (m_width - textWidth) / 2.0f;
    float textY = m_y + (m_height - textHeight) / 2.0f;
    
    renderer.renderText(m_text, textX, textY, 1.0f, textColor);
}

void Button::tick(float deltaTime) {
    // 更新按钮状态
    if (!m_enabled) {
        m_state = State::DISABLED;
    } else if (m_hovered && m_state == State::PRESSED) {
        m_state = State::PRESSED;
    } else if (m_hovered) {
        m_state = State::HOVERED;
    } else {
        m_state = State::NORMAL;
    }
}

bool Button::mousePressed(float mouseX, float mouseY, int button) {
    if (!m_enabled || !m_visible) return false;
    
    if (contains(mouseX, mouseY) && button == 0) {
        m_state = State::PRESSED;
        return true;
    }
    return false;
}

bool Button::mouseReleased(float mouseX, float mouseY, int button) {
    if (!m_enabled || !m_visible) return false;
    
    if (button == 0 && m_state == State::PRESSED) {
        m_state = contains(mouseX, mouseY) ? State::HOVERED : State::NORMAL;
        
        // 如果鼠标还在按钮上，触发点击回调
        if (contains(mouseX, mouseY) && m_onClick) {
            m_onClick();
            return true;
        }
    }
    return false;
}

bool Button::mouseMoved(float mouseX, float mouseY) {
    if (!m_enabled || !m_visible) return false;
    
    bool wasHovered = m_hovered;
    m_hovered = contains(mouseX, mouseY);
    
    if (wasHovered != m_hovered) {
        if (m_hovered) {
            m_state = State::HOVERED;
        } else {
            m_state = State::NORMAL;
        }
        return true;
    }
    return false;
}

} // namespace ui
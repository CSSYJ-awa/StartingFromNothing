#include "Label.h"
#include "UIRenderer.h"

namespace ui {

Label::Label(float x, float y, const std::string& text)
    : Widget(x, y, 0, 0), m_text(text), 
      m_textColor(1.0f, 1.0f, 1.0f),
      m_backgroundColor(0.0f, 0.0f, 0.0f),
      m_backgroundAlpha(0.0f) {}

Label::Label(float x, float y, float width, float height, const std::string& text)
    : Widget(x, y, width, height), m_text(text),
      m_textColor(1.0f, 1.0f, 1.0f),
      m_backgroundColor(0.0f, 0.0f, 0.0f),
      m_backgroundAlpha(0.0f) {}

void Label::render() {
    if (!m_visible) return;
    
    UIRenderer& renderer = UIRenderer::getInstance();
    
    // 如果有背景颜色且不透明，渲染背景
    if (m_backgroundAlpha > 0.0f) {
        renderer.renderRect(m_x, m_y, m_width, m_height, 
                           m_backgroundColor, m_backgroundAlpha);
    }
    
    // 渲染文字
    renderer.renderText(m_text, m_x, m_y, 1.0f, m_textColor);
}

} // namespace ui
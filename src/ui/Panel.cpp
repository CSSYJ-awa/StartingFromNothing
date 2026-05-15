#include "Panel.h"
#include "UIRenderer.h"
#include <algorithm>

namespace ui {

Panel::Panel(float x, float y, float width, float height)
    : Widget(x, y, width, height),
      m_backgroundColor(0.1f, 0.1f, 0.1f),
      m_backgroundAlpha(0.8f) {}

void Panel::init() {
    Widget::init();
    for (auto& child : m_children) {
        child->init();
    }
}

void Panel::tick(float deltaTime) {
    Widget::tick(deltaTime);
    for (auto& child : m_children) {
        if (child->isVisible()) {
            child->tick(deltaTime);
        }
    }
}

void Panel::render() {
    if (!m_visible) return;
    
    UIRenderer& renderer = UIRenderer::getInstance();
    
    // 渲染背景
    if (m_backgroundAlpha > 0.0f) {
        renderer.renderRect(m_x, m_y, m_width, m_height, 
                           m_backgroundColor, m_backgroundAlpha);
    }
    
    // 渲染边框
    renderer.renderRectWithBorder(m_x, m_y, m_width, m_height,
                                  m_backgroundColor, m_backgroundAlpha,
                                  glm::vec3(0.5f, 0.5f, 0.5f), 2.0f);
    
    // 渲染子组件
    for (auto& child : m_children) {
        if (child->isVisible()) {
            child->render();
        }
    }
}

void Panel::removed() {
    Widget::removed();
    for (auto& child : m_children) {
        child->removed();
    }
    m_children.clear();
}

void Panel::addChild(std::shared_ptr<Widget> child) {
    child->setPosition(child->getX() + m_x, child->getY() + m_y);
    m_children.push_back(child);
}

void Panel::removeChild(std::shared_ptr<Widget> child) {
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        m_children.erase(it);
    }
}

void Panel::clearChildren() {
    m_children.clear();
}

bool Panel::mousePressed(float mouseX, float mouseY, int button) {
    if (!m_enabled || !m_visible) return false;
    
    // 将坐标转换为面板相对坐标
    float localX = mouseX - m_x;
    float localY = mouseY - m_y;
    
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            if (child->mousePressed(localX, localY, button)) {
                return true;
            }
        }
    }
    return false;
}

bool Panel::mouseReleased(float mouseX, float mouseY, int button) {
    if (!m_enabled || !m_visible) return false;
    
    float localX = mouseX - m_x;
    float localY = mouseY - m_y;
    
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            if (child->mouseReleased(localX, localY, button)) {
                return true;
            }
        }
    }
    return false;
}

bool Panel::mouseMoved(float mouseX, float mouseY) {
    if (!m_enabled || !m_visible) return false;
    
    float localX = mouseX - m_x;
    float localY = mouseY - m_y;
    
    bool handled = false;
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            handled |= child->mouseMoved(localX, localY);
        }
    }
    return handled;
}

bool Panel::keyPressed(int key, int scancode, int modifiers) {
    if (!m_enabled || !m_visible) return false;
    
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            if (child->keyPressed(key, scancode, modifiers)) {
                return true;
            }
        }
    }
    return false;
}

bool Panel::keyReleased(int key, int scancode, int modifiers) {
    if (!m_enabled || !m_visible) return false;
    
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            if (child->keyReleased(key, scancode, modifiers)) {
                return true;
            }
        }
    }
    return false;
}

bool Panel::charTyped(unsigned int codepoint) {
    if (!m_enabled || !m_visible) return false;
    
    for (auto& child : m_children) {
        if (child->isVisible() && child->isEnabled()) {
            if (child->charTyped(codepoint)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace ui
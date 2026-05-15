#include "Screen.h"
#include "UIRenderer.h"
#include <algorithm>

namespace ui {

void Screen::init() {
    for (auto& widget : m_widgets) {
        widget->init();
    }
}

void Screen::tick(float deltaTime) {
    for (auto& widget : m_widgets) {
        if (widget->isVisible()) {
            widget->tick(deltaTime);
        }
    }
}

void Screen::render() {
    if (!m_open) return;
    
    // 先渲染背景
    renderBackground();
    
    // 按添加顺序渲染所有组件
    for (auto& widget : m_widgets) {
        if (widget->isVisible()) {
            widget->render();
        }
    }
}

void Screen::removed() {
    for (auto& widget : m_widgets) {
        widget->removed();
    }
    clearWidgets();
}

void Screen::renderBackground() {
    // 默认绘制半透明黑色背景
    UIRenderer::getInstance().renderRect(0, 0, 1920, 1080, 
                                         glm::vec3(0.0f, 0.0f, 0.0f), 0.7f);
}

bool Screen::mousePressed(float mouseX, float mouseY, int button) {
    if (!m_open) return false;
    
    // 从后向前遍历，找到最上层的组件
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
        auto& widget = *it;
        if (widget->isVisible() && widget->isEnabled() && widget->contains(mouseX, mouseY)) {
            widget->setFocused(true);
            m_focusedWidget = widget;
            if (widget->mousePressed(mouseX, mouseY, button)) {
                return true;
            }
        }
    }
    return false;
}

bool Screen::mouseReleased(float mouseX, float mouseY, int button) {
    if (!m_open) return false;
    
    if (m_focusedWidget && m_focusedWidget->isEnabled()) {
        if (m_focusedWidget->mouseReleased(mouseX, mouseY, button)) {
            return true;
        }
    }
    
    // 遍历所有组件
    for (auto& widget : m_widgets) {
        if (widget->isVisible() && widget->isEnabled()) {
            if (widget->mouseReleased(mouseX, mouseY, button)) {
                return true;
            }
        }
    }
    return false;
}

bool Screen::mouseMoved(float mouseX, float mouseY) {
    if (!m_open) return false;
    
    bool handled = false;
    for (auto& widget : m_widgets) {
        if (widget->isVisible() && widget->isEnabled()) {
            bool wasHovered = widget->isHovered();
            bool nowHovered = widget->contains(mouseX, mouseY);
            widget->setHovered(nowHovered);
            
            if (wasHovered != nowHovered) {
                handled |= widget->mouseMoved(mouseX, mouseY);
            }
        }
    }
    return handled;
}

bool Screen::keyPressed(int key, int scancode, int modifiers) {
    if (!m_open) return false;
    
    if (m_focusedWidget && m_focusedWidget->isEnabled()) {
        if (m_focusedWidget->keyPressed(key, scancode, modifiers)) {
            return true;
        }
    }
    
    for (auto& widget : m_widgets) {
        if (widget->isVisible() && widget->isEnabled()) {
            if (widget->keyPressed(key, scancode, modifiers)) {
                return true;
            }
        }
    }
    return false;
}

bool Screen::keyReleased(int key, int scancode, int modifiers) {
    if (!m_open) return false;
    
    if (m_focusedWidget && m_focusedWidget->isEnabled()) {
        if (m_focusedWidget->keyReleased(key, scancode, modifiers)) {
            return true;
        }
    }
    
    for (auto& widget : m_widgets) {
        if (widget->isVisible() && widget->isEnabled()) {
            if (widget->keyReleased(key, scancode, modifiers)) {
                return true;
            }
        }
    }
    return false;
}

bool Screen::charTyped(unsigned int codepoint) {
    if (!m_open) return false;
    
    if (m_focusedWidget && m_focusedWidget->isEnabled()) {
        if (m_focusedWidget->charTyped(codepoint)) {
            return true;
        }
    }
    
    for (auto& widget : m_widgets) {
        if (widget->isVisible() && widget->isEnabled()) {
            if (widget->charTyped(codepoint)) {
                return true;
            }
        }
    }
    return false;
}

void Screen::addWidget(std::shared_ptr<Widget> widget) {
    m_widgets.push_back(widget);
}

void Screen::removeWidget(std::shared_ptr<Widget> widget) {
    auto it = std::find(m_widgets.begin(), m_widgets.end(), widget);
    if (it != m_widgets.end()) {
        m_widgets.erase(it);
    }
}

void Screen::clearWidgets() {
    m_widgets.clear();
    m_focusedWidget = nullptr;
}

void Screen::setOpen(bool open) {
    m_open = open;
    if (m_open) {
        init();
    } else {
        removed();
    }
}

} // namespace ui
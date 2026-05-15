#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace ui {

class Widget {
public:
    Widget(float x, float y, float width, float height)
        : m_x(x), m_y(y), m_width(width), m_height(height),
          m_visible(true), m_enabled(true), m_hovered(false), m_focused(false) {}
    
    virtual ~Widget() = default;
    
    // 生命周期方法
    virtual void init() {}
    virtual void tick(float deltaTime) {}
    virtual void render() = 0;
    virtual void removed() {}
    
    // 输入事件处理
    virtual bool mousePressed(float mouseX, float mouseY, int button) { return false; }
    virtual bool mouseReleased(float mouseX, float mouseY, int button) { return false; }
    virtual bool mouseMoved(float mouseX, float mouseY) { return false; }
    virtual bool keyPressed(int key, int scancode, int modifiers) { return false; }
    virtual bool keyReleased(int key, int scancode, int modifiers) { return false; }
    virtual bool charTyped(unsigned int codepoint) { return false; }
    
    // 获取位置和尺寸
    float getX() const { return m_x; }
    float getY() const { return m_y; }
    float getWidth() const { return m_width; }
    float getHeight() const { return m_height; }
    
    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void setSize(float width, float height) { m_width = width; m_height = height; }
    
    // 状态控制
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    
    bool isHovered() const { return m_hovered; }
    void setHovered(bool hovered) { m_hovered = hovered; }
    
    bool isFocused() const { return m_focused; }
    void setFocused(bool focused) { m_focused = focused; }
    
    // 检查点是否在组件内
    bool contains(float x, float y) const {
        return x >= m_x && x <= m_x + m_width && y >= m_y && y <= m_y + m_height;
    }

protected:
    float m_x, m_y;
    float m_width, m_height;
    bool m_visible;
    bool m_enabled;
    bool m_hovered;
    bool m_focused;
};

} // namespace ui
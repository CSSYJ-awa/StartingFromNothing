#pragma once

#include "Widget.h"
#include <vector>
#include <memory>

namespace ui {

class Panel : public Widget {
public:
    Panel(float x, float y, float width, float height);
    
    void init() override;
    void tick(float deltaTime) override;
    void render() override;
    void removed() override;
    
    // 子组件管理
    void addChild(std::shared_ptr<Widget> child);
    void removeChild(std::shared_ptr<Widget> child);
    void clearChildren();
    
    // 输入事件会传递给子组件
    bool mousePressed(float mouseX, float mouseY, int button) override;
    bool mouseReleased(float mouseX, float mouseY, int button) override;
    bool mouseMoved(float mouseX, float mouseY) override;
    bool keyPressed(int key, int scancode, int modifiers) override;
    bool keyReleased(int key, int scancode, int modifiers) override;
    bool charTyped(unsigned int codepoint) override;
    
    // 设置背景颜色
    void setBackgroundColor(const glm::vec3& color, float alpha = 1.0f) {
        m_backgroundColor = color;
        m_backgroundAlpha = alpha;
    }
    
private:
    std::vector<std::shared_ptr<Widget>> m_children;
    glm::vec3 m_backgroundColor;
    float m_backgroundAlpha;
};

} // namespace ui
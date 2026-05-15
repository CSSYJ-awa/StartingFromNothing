#pragma once

#include "Widget.h"
#include <vector>
#include <memory>

namespace ui {

class Screen {
public:
    Screen() : m_open(false), m_focusedWidget(nullptr) {}
    virtual ~Screen() = default;
    
    // 生命周期方法
    virtual void init();
    virtual void tick(float deltaTime);
    virtual void render();
    virtual void removed();
    
    // 输入事件分发
    bool mousePressed(float mouseX, float mouseY, int button);
    bool mouseReleased(float mouseX, float mouseY, int button);
    bool mouseMoved(float mouseX, float mouseY);
    bool keyPressed(int key, int scancode, int modifiers);
    bool keyReleased(int key, int scancode, int modifiers);
    bool charTyped(unsigned int codepoint);
    
    // 组件管理
    void addWidget(std::shared_ptr<Widget> widget);
    void removeWidget(std::shared_ptr<Widget> widget);
    void clearWidgets();
    
    // 界面状态
    bool isOpen() const { return m_open; }
    void setOpen(bool open);
    
    // 子类可重写的虚方法
    virtual bool isModal() const { return true; }  // 默认是模态界面
    virtual bool shouldHideHUD() const { return true; }  // 默认隐藏HUD
    
protected:
    // 背景渲染（子类可重写）
    virtual void renderBackground();
    
    std::vector<std::shared_ptr<Widget>> m_widgets;
    std::shared_ptr<Widget> m_focusedWidget;
    bool m_open;
};

} // namespace ui
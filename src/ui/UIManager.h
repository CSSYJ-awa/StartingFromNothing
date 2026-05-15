#pragma once

#include "Screen.h"
#include <memory>
#include <vector>

namespace ui {

enum class UILayer {
    HUD = 0,        // 最底层：抬头显示
    NON_MODAL,      // 非模态界面（如调试界面）
    MODAL           // 最顶层：模态界面
};

class UIManager {
public:
    static UIManager& getInstance();
    
    // 添加界面到指定层
    void addScreen(std::shared_ptr<Screen> screen, UILayer layer);
    
    // 移除界面
    void removeScreen(std::shared_ptr<Screen> screen);
    
    // 获取当前打开的模态界面
    std::shared_ptr<Screen> getCurrentModalScreen() const;
    
    // 检查是否有模态界面打开
    bool hasModalScreen() const;
    
    // 更新所有界面
    void tick(float deltaTime);
    
    // 渲染所有界面（按层次顺序）
    void render();
    
    // 处理输入事件
    bool handleMousePressed(float mouseX, float mouseY, int button);
    bool handleMouseReleased(float mouseX, float mouseY, int button);
    bool handleMouseMoved(float mouseX, float mouseY);
    bool handleKeyPressed(int key, int scancode, int modifiers);
    bool handleKeyReleased(int key, int scancode, int modifiers);
    bool handleCharTyped(unsigned int codepoint);
    
    // 获取当前鼠标位置
    glm::vec2 getMousePosition() const { return m_mousePosition; }
    
    // 设置鼠标位置
    void setMousePosition(float x, float y) { m_mousePosition = glm::vec2(x, y); }
    
private:
    UIManager() = default;
    ~UIManager() = default;
    
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;
    
    std::vector<std::shared_ptr<Screen>> m_hudScreens;
    std::vector<std::shared_ptr<Screen>> m_nonModalScreens;
    std::vector<std::shared_ptr<Screen>> m_modalScreens;
    
    glm::vec2 m_mousePosition;
};

} // namespace ui
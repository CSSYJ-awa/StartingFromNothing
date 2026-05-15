#pragma once

#include "Screen.h"
#include "Button.h"
#include <functional>

namespace ui {

class MainMenuScreen : public Screen {
public:
    MainMenuScreen();
    
    void init() override;
    void render() override;
    
    // 设置回调函数
    void setOnExit(std::function<void()> callback) { m_onExit = callback; }
    void setOnResume(std::function<void()> callback) { m_onResume = callback; }
    
private:
    std::shared_ptr<Button> m_exitButton;
    std::shared_ptr<Button> m_resumeButton;
    
    std::function<void()> m_onExit;
    std::function<void()> m_onResume;
};

} // namespace ui
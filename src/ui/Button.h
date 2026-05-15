#pragma once

#include "Widget.h"
#include <string>

namespace ui {

class Button : public Widget {
public:
    enum class State {
        NORMAL,
        HOVERED,
        PRESSED,
        DISABLED
    };
    
    Button(float x, float y, float width, float height, const std::string& text);
    
    void render() override;
    void tick(float deltaTime) override;
    
    bool mousePressed(float mouseX, float mouseY, int button) override;
    bool mouseReleased(float mouseX, float mouseY, int button) override;
    bool mouseMoved(float mouseX, float mouseY) override;
    
    // 设置回调函数
    void setOnClick(std::function<void()> callback) { m_onClick = callback; }
    
    // 设置按钮文本
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }
    
private:
    std::string m_text;
    State m_state;
    std::function<void()> m_onClick;
};

} // namespace ui
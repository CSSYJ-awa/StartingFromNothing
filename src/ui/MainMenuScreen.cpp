#include "MainMenuScreen.h"
#include "UIRenderer.h"
#include <glm/glm.hpp>

namespace ui {

MainMenuScreen::MainMenuScreen() {
    // 默认是模态界面
}

void MainMenuScreen::init() {
    Screen::init();
    
    // 创建按钮
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    
    // 退出按钮
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f;
    float buttonY = screenHeight / 2.0f;
    
    m_exitButton = std::make_shared<Button>(
        screenWidth / 2.0f - buttonWidth / 2.0f,
        buttonY,
        buttonWidth,
        buttonHeight,
        "EXIT"
    );
    
    m_exitButton->setOnClick([this]() {
        if (m_onExit) {
            m_onExit();
        }
    });
    
    addWidget(m_exitButton);
    
    // 继续按钮（在退出按钮上方）
    m_resumeButton = std::make_shared<Button>(
        screenWidth / 2.0f - buttonWidth / 2.0f,
        buttonY - buttonHeight - 20.0f,
        buttonWidth,
        buttonHeight,
        "RESUME"
    );
    
    m_resumeButton->setOnClick([this]() {
        if (m_onResume) {
            m_onResume();
        }
    });
    
    addWidget(m_resumeButton);
}

void MainMenuScreen::render() {
    if (!m_open) return;
    
    // 渲染背景（半透明灰色）
    UIRenderer::getInstance().renderRect(0, 0, 1920, 1080, 
                                         glm::vec3(0.1f, 0.1f, 0.1f), 0.8f);
    
    // 渲染标题
    UIRenderer::getInstance().renderText("Voxel Game", 1920 / 2.0f - 100, 200, 
                                         2.0f, glm::vec3(1.0f, 1.0f, 1.0f));
    
    // 渲染所有子组件
    Screen::render();
}

} // namespace ui
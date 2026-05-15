#include "UIManager.h"
#include <algorithm>

namespace ui {

UIManager& UIManager::getInstance() {
    static UIManager instance;
    return instance;
}

void UIManager::addScreen(std::shared_ptr<Screen> screen, UILayer layer) {
    switch (layer) {
        case UILayer::HUD:
            m_hudScreens.push_back(screen);
            break;
        case UILayer::NON_MODAL:
            m_nonModalScreens.push_back(screen);
            break;
        case UILayer::MODAL:
            m_modalScreens.push_back(screen);
            break;
    }
}

void UIManager::removeScreen(std::shared_ptr<Screen> screen) {
    // 从所有层中移除
    auto removeFromLayer = [&](std::vector<std::shared_ptr<Screen>>& layer) {
        auto it = std::find(layer.begin(), layer.end(), screen);
        if (it != layer.end()) {
            layer.erase(it);
        }
    };
    
    removeFromLayer(m_hudScreens);
    removeFromLayer(m_nonModalScreens);
    removeFromLayer(m_modalScreens);
}

std::shared_ptr<Screen> UIManager::getCurrentModalScreen() const {
    if (!m_modalScreens.empty()) {
        return m_modalScreens.back();
    }
    return nullptr;
}

bool UIManager::hasModalScreen() const {
    return !m_modalScreens.empty();
}

void UIManager::tick(float deltaTime) {
    // 更新顺序：HUD -> 非模态 -> 模态
    for (auto& screen : m_hudScreens) {
        // 如果有模态界面且该 HUD 应该被隐藏，则跳过
        if (!hasModalScreen() || !screen->shouldHideHUD()) {
            screen->tick(deltaTime);
        }
    }
    
    for (auto& screen : m_nonModalScreens) {
        screen->tick(deltaTime);
    }
    
    for (auto& screen : m_modalScreens) {
        screen->tick(deltaTime);
    }
}

void UIManager::render() {
    // 渲染顺序：HUD -> 非模态 -> 模态（后渲染的覆盖先渲染的）
    
    // 渲染 HUD（如果没有模态界面或模态界面不隐藏 HUD）
    bool shouldRenderHUD = !hasModalScreen();
    if (!shouldRenderHUD && !m_modalScreens.empty()) {
        shouldRenderHUD = !m_modalScreens.back()->shouldHideHUD();
    }
    
    if (shouldRenderHUD) {
        for (auto& screen : m_hudScreens) {
            screen->render();
        }
    }
    
    // 渲染非模态界面
    for (auto& screen : m_nonModalScreens) {
        screen->render();
    }
    
    // 渲染模态界面
    for (auto& screen : m_modalScreens) {
        screen->render();
    }
}

bool UIManager::handleMousePressed(float mouseX, float mouseY, int button) {
    m_mousePosition = glm::vec2(mouseX, mouseY);
    
    // 事件分发顺序：模态 -> 非模态 -> HUD（从顶层到底层）
    // 模态界面优先处理
    
    // 如果有模态界面，只处理模态界面的事件
    if (!m_modalScreens.empty()) {
        for (auto it = m_modalScreens.rbegin(); it != m_modalScreens.rend(); ++it) {
            if ((*it)->mousePressed(mouseX, mouseY, button)) {
                return true;
            }
        }
        return true; // 模态界面拦截所有事件
    }
    
    // 处理非模态界面
    for (auto it = m_nonModalScreens.rbegin(); it != m_nonModalScreens.rend(); ++it) {
        if ((*it)->mousePressed(mouseX, mouseY, button)) {
            return true;
        }
    }
    
    // 处理 HUD
    for (auto it = m_hudScreens.rbegin(); it != m_hudScreens.rend(); ++it) {
        if ((*it)->mousePressed(mouseX, mouseY, button)) {
            return true;
        }
    }
    
    return false;
}

bool UIManager::handleMouseReleased(float mouseX, float mouseY, int button) {
    m_mousePosition = glm::vec2(mouseX, mouseY);
    
    if (!m_modalScreens.empty()) {
        for (auto it = m_modalScreens.rbegin(); it != m_modalScreens.rend(); ++it) {
            if ((*it)->mouseReleased(mouseX, mouseY, button)) {
                return true;
            }
        }
        return true;
    }
    
    for (auto it = m_nonModalScreens.rbegin(); it != m_nonModalScreens.rend(); ++it) {
        if ((*it)->mouseReleased(mouseX, mouseY, button)) {
            return true;
        }
    }
    
    for (auto it = m_hudScreens.rbegin(); it != m_hudScreens.rend(); ++it) {
        if ((*it)->mouseReleased(mouseX, mouseY, button)) {
            return true;
        }
    }
    
    return false;
}

bool UIManager::handleMouseMoved(float mouseX, float mouseY) {
    m_mousePosition = glm::vec2(mouseX, mouseY);
    
    bool handled = false;
    
    if (!m_modalScreens.empty()) {
        for (auto& screen : m_modalScreens) {
            handled |= screen->mouseMoved(mouseX, mouseY);
        }
        return handled;
    }
    
    for (auto& screen : m_nonModalScreens) {
        handled |= screen->mouseMoved(mouseX, mouseY);
    }
    
    for (auto& screen : m_hudScreens) {
        handled |= screen->mouseMoved(mouseX, mouseY);
    }
    
    return handled;
}

bool UIManager::handleKeyPressed(int key, int scancode, int modifiers) {
    if (!m_modalScreens.empty()) {
        for (auto it = m_modalScreens.rbegin(); it != m_modalScreens.rend(); ++it) {
            if ((*it)->keyPressed(key, scancode, modifiers)) {
                return true;
            }
        }
        return true;
    }
    
    for (auto it = m_nonModalScreens.rbegin(); it != m_nonModalScreens.rend(); ++it) {
        if ((*it)->keyPressed(key, scancode, modifiers)) {
            return true;
        }
    }
    
    for (auto it = m_hudScreens.rbegin(); it != m_hudScreens.rend(); ++it) {
        if ((*it)->keyPressed(key, scancode, modifiers)) {
            return true;
        }
    }
    
    return false;
}

bool UIManager::handleKeyReleased(int key, int scancode, int modifiers) {
    if (!m_modalScreens.empty()) {
        for (auto it = m_modalScreens.rbegin(); it != m_modalScreens.rend(); ++it) {
            if ((*it)->keyReleased(key, scancode, modifiers)) {
                return true;
            }
        }
        return true;
    }
    
    for (auto it = m_nonModalScreens.rbegin(); it != m_nonModalScreens.rend(); ++it) {
        if ((*it)->keyReleased(key, scancode, modifiers)) {
            return true;
        }
    }
    
    for (auto it = m_hudScreens.rbegin(); it != m_hudScreens.rend(); ++it) {
        if ((*it)->keyReleased(key, scancode, modifiers)) {
            return true;
        }
    }
    
    return false;
}

bool UIManager::handleCharTyped(unsigned int codepoint) {
    if (!m_modalScreens.empty()) {
        for (auto it = m_modalScreens.rbegin(); it != m_modalScreens.rend(); ++it) {
            if ((*it)->charTyped(codepoint)) {
                return true;
            }
        }
        return true;
    }
    
    for (auto it = m_nonModalScreens.rbegin(); it != m_nonModalScreens.rend(); ++it) {
        if ((*it)->charTyped(codepoint)) {
            return true;
        }
    }
    
    for (auto it = m_hudScreens.rbegin(); it != m_hudScreens.rend(); ++it) {
        if ((*it)->charTyped(codepoint)) {
            return true;
        }
    }
    
    return false;
}

} // namespace ui
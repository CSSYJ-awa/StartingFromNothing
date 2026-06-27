/**
 * player.h —— 玩家控制器
 *
 * 实现第一人称玩家控制，包括：
 * 1. WASD 移动 + 鼠标视角
 * 2. 重力 + 跳跃
 * 3. AABB 碰撞检测与滑动
 * 4. 与地形的精确碰撞
 *
 * 【碰撞检测算法】
 * 使用 AABB（轴对齐包围盒）进行碰撞检测：
 * 1. 将玩家速度分解为 X/Y/Z 三个分量
 * 2. 分别对每个分量进行碰撞检测和响应
 * 3. 检测玩家 AABB 扩展后的区域内的所有方块
 * 4. 如有碰撞，将对应速度分量置零并调整位置
 * 5. 这种"分别处理各轴"的方式可以支持沿墙面滑动
 */
#pragma once

#include "camera.h"
#include "chunk.h"

struct GLFWwindow; // 前向声明，避免在头文件中包含 GLFW

#include <glm/glm.hpp>

// ============================================================================
// Player —— 玩家
// ============================================================================
class Player
{
public:
    Player();

    /** 每帧更新 */
    void update(float deltaTime, GLFWwindow* window);

    /** 处理鼠标移动 */
    void onMouseMove(float xpos, float ypos);

    /** 获取摄像机引用 */
    Camera& getCamera() { return m_camera; }
    const Camera& getCamera() const { return m_camera; }

    /** 获取玩家位置 */
    glm::vec3 getPosition() const { return m_position; }

    /** 获取玩家 AABB */
    std::pair<glm::vec3, glm::vec3> getAABB() const;

    /** 设置玩家位置 */
    void setPosition(const glm::vec3& pos) { m_position = pos; }

    /** 获取玩家是否在地面 */
    bool isOnGround() const { return m_onGround; }

    /** 获取速度 */
    glm::vec3 getVelocity() const { return m_velocity; }

    /** 跳跃 */
    void jump();

    /** 鼠标捕获状态 */
    void setMouseCaptured(bool captured) { m_mouseCaptured = captured; }
    bool isMouseCaptured() const { return m_mouseCaptured; }

private:
    // ---- 物理属性 ----
    glm::vec3 m_position;
    glm::vec3 m_velocity;
    bool      m_onGround;

    // ---- 碰撞体尺寸 ----
    static constexpr float PLAYER_WIDTH  = 0.6f;  // 碰撞体宽度
    static constexpr float PLAYER_HEIGHT = 1.8f;  // 碰撞体高度
    static constexpr float PLAYER_EYE    = 1.6f;  // 眼睛高度（相对于脚底）

    // ---- 移动参数 ----
    static constexpr float WALK_SPEED    = 4.5f;  // 步行速度 (m/s)
    static constexpr float SPRINT_SPEED  = 7.0f;  // 冲刺速度 (m/s)
    static constexpr float JUMP_SPEED    = 9.5f;  // 跳跃初速度 (配合重力 35, 跳高 ≈ 1.29 格)
    static constexpr float GRAVITY       = 35.0f; // 重力加速度 (m/s²)
    static constexpr float EPSILON       = 0.001f;

    // ---- 摄像机 ----
    Camera m_camera;
    bool   m_firstMouse = true;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool   m_mouseCaptured = true;

    // ---- 按键状态 ----
    bool m_keyW = false, m_keyA = false;
    bool m_keyS = false, m_keyD = false;
    bool m_keySpace = false;
    bool m_keyShift = false;

    /**
     * AABB 与地形的碰撞检测
     * @param pos 玩家位置
     * @param vel 速度（输入/输出，发生碰撞的方向置零）
     * @return 是否发生碰撞
     */
    bool collideWithTerrain(glm::vec3& pos, glm::vec3& vel);

    /**
     * 检测单个方块与 AABB 的碰撞
     */
    bool aabbIntersectsBlock(
        const glm::vec3& aabbMin, const glm::vec3& aabbMax,
        const glm::ivec3& blockPos) const;
};

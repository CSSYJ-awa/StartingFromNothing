/**
 * player.cpp —— 玩家控制器实现
 */
#include "player.h"
#include "block_registry.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

Player::Player()
    : m_position(0.0f, 60.0f, 0.0f) // 初始出生位置
    , m_velocity(0.0f)
    , m_onGround(false)
    , m_camera(75.0f, 0.1f, 800.0f)
{
    m_camera.setPosition(m_position + glm::vec3(0.0f, PLAYER_EYE, 0.0f));
}

void Player::update(float deltaTime, GLFWwindow* window)
{
    // ---- 限制最大 deltaTime 防止跳跃过大 ----
    deltaTime = std::min(deltaTime, 0.05f);

    // ---- 获取输入 ----
    m_keyW      = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    m_keyA      = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    m_keyS      = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    m_keyD      = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    m_keySpace  = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    m_keyShift  = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

    // ---- 计算移动方向 ----
    glm::vec3 front = m_camera.getFront();
    glm::vec3 right = m_camera.getRight();

    // 水平移动（忽略垂直分量）
    front.y = 0.0f;
    front = glm::normalize(front);
    right.y = 0.0f;
    right = glm::normalize(right);

    glm::vec3 moveDir(0.0f);
    if (m_keyW) moveDir += front;
    if (m_keyS) moveDir -= front;
    if (m_keyA) moveDir -= right;
    if (m_keyD) moveDir += right;

    if (glm::length(moveDir) > 0.0f)
        moveDir = glm::normalize(moveDir);

    float speed = m_keyShift ? SPRINT_SPEED : WALK_SPEED;

    // ---- 水平速度 ----
    glm::vec3 targetVel = moveDir * speed;
    m_velocity.x = targetVel.x;
    m_velocity.z = targetVel.z;

    // ---- 垂直速度（重力）- 仅在空中时施加 ----
    if (!m_onGround)
    {
        m_velocity.y -= GRAVITY * deltaTime;
    }
    else
    {
        // 地面时阻止继续下沉
        if (m_velocity.y < 0.0f)
            m_velocity.y = 0.0f;
    }

    // ---- 跳跃 ----
    if (m_keySpace && m_onGround)
    {
        m_velocity.y = JUMP_SPEED;
        m_onGround = false;
    }

    // ---- 计算新位置：先移动，后碰撞解析 ----
    glm::vec3 newPos = m_position + m_velocity * deltaTime;

    // ---- 碰撞检测与响应 ----
    collideWithTerrain(newPos, m_velocity);

    // ---- 更新位置 ----
    m_position = newPos;

    // ---- 死亡跌落检测 ----
    if (m_position.y < -100.0f)
    {
        m_position = glm::vec3(0.0f, 60.0f, 0.0f);
        m_velocity = glm::vec3(0.0f);
    }

    // ---- 更新摄像机位置 ----
    m_camera.setPosition(m_position + glm::vec3(0.0f, PLAYER_EYE, 0.0f));
}

void Player::onMouseMove(float xpos, float ypos)
{
    if (!m_mouseCaptured)
        return;
    (void)xpos; (void)ypos;

    if (m_firstMouse)
    {
        m_lastMouseX = xpos;
        m_lastMouseY = ypos;
        m_firstMouse = false;
        return;
    }

    double dx = xpos - m_lastMouseX;
    double dy = ypos - m_lastMouseY;

    m_lastMouseX = xpos;
    m_lastMouseY = ypos;

    m_camera.rotate(static_cast<float>(dx), static_cast<float>(dy));
}

std::pair<glm::vec3, glm::vec3> Player::getAABB() const
{
    glm::vec3 halfWidth(PLAYER_WIDTH * 0.5f, 0.0f, PLAYER_WIDTH * 0.5f);
    glm::vec3 min = m_position - halfWidth;
    glm::vec3 max = m_position + halfWidth;
    max.y += PLAYER_HEIGHT;
    return {min, max};
}

void Player::jump()
{
    if (m_onGround)
    {
        m_velocity.y = JUMP_SPEED;
        m_onGround = false;
    }
}

/**
 * AABB 碰撞检测实现
 *
 * 【算法说明】
 * 1. 分别处理 X/Y/Z 三轴，实现沿墙面滑动
 * 2. 对于每个轴，按速度方向遍历方块确保最近的阻挡物先被解析
 * 3. 每解析完一个轴，重新计算 AABB
 * 4. vel == 0 时强制将玩家推出重叠的方块
 */
bool Player::collideWithTerrain(glm::vec3& pos, glm::vec3& vel)
{
    constexpr float HALF_W = PLAYER_WIDTH * 0.5f;
    const auto& registry = BlockRegistry::instance();

    // 每帧开始时重置地面标志，只有本帧碰撞检测到地面才设为 true
    m_onGround = false;
    bool collided = false;
    bool wasFalling = (vel.y < 0.0f);
    bool autoJumped = false;

    // ========== Y 轴优先（重力/地面先处理）==========
    {
        glm::vec3 min = pos - glm::vec3(HALF_W, 0.0f, HALF_W);
        glm::vec3 max = pos + glm::vec3(HALF_W, 0.0f, HALF_W);
        max.y += PLAYER_HEIGHT;

        int x0 = static_cast<int>(std::floor(min.x));
        int y0 = static_cast<int>(std::floor(min.y));
        int z0 = static_cast<int>(std::floor(min.z));
        int x1 = static_cast<int>(std::floor(max.x));
        int y1 = static_cast<int>(std::floor(max.y));
        int z1 = static_cast<int>(std::floor(max.z));

        int yB = (vel.y > 0) ? y1 : y0, yE = (vel.y > 0) ? y0 : y1, yS = (vel.y > 0) ? -1 : 1;
        for (int by = yB; (vel.y > 0) ? (by >= yE) : (by <= yE); by += yS)
        for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
        {
            if (!registry.isSolid(Chunk::getBlockAt({bx, by, bz}))) continue;
            glm::vec3 bMin(bx, by, bz), bMax(bx+1, by+1, bz+1);
            if (max.x <= bMin.x || min.x >= bMax.x ||
                max.y <= bMin.y || min.y >= bMax.y ||
                max.z <= bMin.z || min.z >= bMax.z) continue;

            // 【关键】仅处理玩家中心正下方/正上方的方块
            // 侧面的墙壁/台阶方块由 X/Z 轴处理（自动跨步 / 自动跳跃）
            // 方块范围是 [bx, bx+1) 的半开区间
            if (pos.x < bMin.x || pos.x >= bMax.x ||
                pos.z < bMin.z || pos.z >= bMax.z)
                continue;

            // ---- 天花板碰撞（向上移动时头顶撞到方块底部）----
            if (vel.y > 0.0f)
            {
                float headY = pos.y + PLAYER_HEIGHT;
                // 仅当玩家的头顶低于方块底面时才视为天花板碰撞
                if (headY <= bMin.y + 0.1f)
                {
                    pos.y = bMin.y - PLAYER_HEIGHT;
                    vel.y = 0;
                    collided = true;
                    goto y_done;
                }
                // 否则跳过（玩家正在跳跃穿过这个方块）
                continue;
            }
            // ---- 地面/脚下碰撞 ----
            else
            {
                pos.y = bMax.y;
                vel.y = 0;
                if (wasFalling) m_onGround = true;
                collided = true;
                goto y_done;
            }
        }
    }
    y_done:;

    // ========== X 轴 ==========
    if (vel.x != 0.0f)
    {
        glm::vec3 min = pos - glm::vec3(HALF_W, 0.0f, HALF_W);
        glm::vec3 max = pos + glm::vec3(HALF_W, 0.0f, HALF_W);
        max.y += PLAYER_HEIGHT;

        int x0 = static_cast<int>(std::floor(min.x));
        int y0 = static_cast<int>(std::floor(min.y));
        int z0 = static_cast<int>(std::floor(min.z));
        int x1 = static_cast<int>(std::floor(max.x));
        int y1 = static_cast<int>(std::floor(max.y));
        int z1 = static_cast<int>(std::floor(max.z));

        int xB = (vel.x > 0) ? x1 : x0, xE = (vel.x > 0) ? x0 : x1, xS = (vel.x > 0) ? -1 : 1;
        for (int bx = xB; (vel.x > 0) ? (bx >= xE) : (bx <= xE); bx += xS)
        for (int by = y0; by <= y1; ++by)
        for (int bz = z0; bz <= z1; ++bz)
        {
            if (!registry.isSolid(Chunk::getBlockAt({bx, by, bz}))) continue;
            glm::vec3 bMin(bx, by, bz), bMax(bx+1, by+1, bz+1);
            if (max.x <= bMin.x || min.x >= bMax.x ||
                max.y <= bMin.y || min.y >= bMax.y ||
                max.z <= bMin.z || min.z >= bMax.z) continue;

            // ---- 自动跨步（Step-Up）----
            // 跨步高度 = 从玩家脚底到阻挡方块堆叠后的顶部
            float stepTargetY = bMax.y;
            int checkY = by + 1;
            while (checkY < by + 3 && registry.isSolid(Chunk::getBlockAt({bx, checkY, bz})))
            {
                stepTargetY = static_cast<float>(checkY + 1);
                checkY++;
            }
            float stepH = stepTargetY - pos.y;

            // 只对 ≤ 0.5 格的小台阶自动跨步（防止跨过整面墙）
            if (stepH > 0.0f && stepH <= 0.5f)
            {
                // 确认步上有空间容纳玩家的高度
                glm::ivec3 headCheck(bx, static_cast<int>(stepTargetY), bz);
                bool headroom = true;
                for (int hy = 0; hy < static_cast<int>(PLAYER_HEIGHT) + 1; ++hy)
                {
                    glm::ivec3 check(headCheck.x, headCheck.y + hy, headCheck.z);
                    if (registry.isSolid(Chunk::getBlockAt(check)))
                    { headroom = false; break; }
                }
                if (headroom)
                {
                    pos.y = stepTargetY;     // 跨步到堆叠顶部
                    m_onGround = true;
                    continue;                // 不阻挡水平移动
                }
            }

            // ---- 自动跳跃：台阶过高时自动跳跃 ----
            if (!autoJumped && stepH > 0.5f && stepH <= 1.2f && wasFalling == false)
            {
                glm::ivec3 headCheck(bx, static_cast<int>(stepTargetY + PLAYER_HEIGHT + 0.5f), bz);
                bool headroom = true;
                for (int hy = 0; hy < static_cast<int>(PLAYER_HEIGHT) + 2; ++hy)
                {
                    if (registry.isSolid(Chunk::getBlockAt({headCheck.x, headCheck.y + hy, headCheck.z})))
                    { headroom = false; break; }
                }
                if (headroom)
                {
                    vel.y = JUMP_SPEED;
                    m_onGround = false;
                    autoJumped = true;
                    continue;
                }
            }

            // ---- 跳跃中不阻挡水平移动 ----
            // 如果玩家正在上升（vel.y > 0），说明处于跳跃中，
            // 允许水平通过阻挡方块，跳跃惯性自然会带玩家越过障碍
            if (vel.y > 0.0f)
                continue;

            // ---- 阻挡 ----
            if (vel.x > 0.0f)      { pos.x = bMin.x - HALF_W; vel.x = 0; }
            else                   { pos.x = bMax.x + HALF_W; vel.x = 0; }
            collided = true; goto x_done;
        }
    }
    x_done:;

    // ========== Z 轴 ==========
    if (vel.z != 0.0f)
    {
        glm::vec3 min = pos - glm::vec3(HALF_W, 0.0f, HALF_W);
        glm::vec3 max = pos + glm::vec3(HALF_W, 0.0f, HALF_W);
        max.y += PLAYER_HEIGHT;

        int x0 = static_cast<int>(std::floor(min.x));
        int y0 = static_cast<int>(std::floor(min.y));
        int z0 = static_cast<int>(std::floor(min.z));
        int x1 = static_cast<int>(std::floor(max.x));
        int y1 = static_cast<int>(std::floor(max.y));
        int z1 = static_cast<int>(std::floor(max.z));

        int zB = (vel.z > 0) ? z1 : z0, zE = (vel.z > 0) ? z0 : z1, zS = (vel.z > 0) ? -1 : 1;
        for (int bz = zB; (vel.z > 0) ? (bz >= zE) : (bz <= zE); bz += zS)
        for (int bx = x0; bx <= x1; ++bx)
        for (int by = y0; by <= y1; ++by)
        {
            if (!registry.isSolid(Chunk::getBlockAt({bx, by, bz}))) continue;
            glm::vec3 bMin(bx, by, bz), bMax(bx+1, by+1, bz+1);
            if (max.x <= bMin.x || min.x >= bMax.x ||
                max.y <= bMin.y || min.y >= bMax.y ||
                max.z <= bMin.z || min.z >= bMax.z) continue;

            // ---- 自动跨步（Step-Up）----
            float stepTargetY = bMax.y;
            int checkY = by + 1;
            while (checkY < by + 3 && registry.isSolid(Chunk::getBlockAt({bx, checkY, bz})))
            {
                stepTargetY = static_cast<float>(checkY + 1);
                checkY++;
            }
            float stepH = stepTargetY - pos.y;
            if (stepH > 0.0f && stepH <= 0.5f)
            {
                glm::ivec3 headCheck(bx, static_cast<int>(stepTargetY), bz);
                bool headroom = true;
                for (int hy = 0; hy < static_cast<int>(PLAYER_HEIGHT) + 1; ++hy)
                {
                    if (registry.isSolid(Chunk::getBlockAt({headCheck.x, headCheck.y + hy, headCheck.z})))
                    { headroom = false; break; }
                }
                if (headroom)
                {
                    pos.y = stepTargetY;
                    m_onGround = true;
                    continue;
                }
            }
            // ---- 自动跳跃：台阶过高时自动跳跃 ----
            if (!autoJumped && stepH > 0.5f && stepH <= 1.2f && wasFalling == false)
            {
                // 检查跳跃后头顶空间
                glm::ivec3 headCheck(bx, static_cast<int>(stepTargetY + PLAYER_HEIGHT + 0.5f), bz);
                bool headroom = true;
                for (int hy = 0; hy < static_cast<int>(PLAYER_HEIGHT) + 2; ++hy)
                {
                    if (registry.isSolid(Chunk::getBlockAt({headCheck.x, headCheck.y + hy, headCheck.z})))
                    { headroom = false; break; }
                }
                if (headroom)
                {
                    vel.y = JUMP_SPEED;
                    m_onGround = false;
                    autoJumped = true;
                    // 不阻挡水平移动，让跳跃惯性带玩家上去
                    continue;
                }
            }

            // ---- 跳跃中不阻挡水平移动 ----
            if (vel.y > 0.0f)
                continue;

            // ---- 阻挡 ----
            if (vel.z > 0.0f)      { pos.z = bMin.z - HALF_W; vel.z = 0; }
            else                   { pos.z = bMax.z + HALF_W; vel.z = 0; }
            collided = true; goto z_done;
        }
    }
    z_done:;

    return collided;
}

bool Player::aabbIntersectsBlock(
    const glm::vec3& aabbMin, const glm::vec3& aabbMax,
    const glm::ivec3& blockPos) const
{
    glm::vec3 blockMin(blockPos);
    glm::vec3 blockMax(blockPos + glm::ivec3(1));

    return !(aabbMax.x <= blockMin.x || aabbMin.x >= blockMax.x ||
             aabbMax.y <= blockMin.y || aabbMin.y >= blockMax.y ||
             aabbMax.z <= blockMin.z || aabbMin.z >= blockMax.z);
}

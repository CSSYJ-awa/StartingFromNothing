/**
 * camera.h —— 第一人称摄像机
 *
 * 管理视角变换，提供 view 和 projection 矩阵。
 * 支持第一人称欧拉角控制（偏航/俯仰）。
 */
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>

// ============================================================================
// Camera —— 第一人称摄像机
// ============================================================================
class Camera
{
public:
    /**
     * @param fovDegrees 视野角度（度）
     * @param nearPlane  近裁剪面
     * @param farPlane   远裁剪面
     */
    Camera(float fovDegrees = 75.0f, float nearPlane = 0.1f, float farPlane = 1000.0f)
        : m_position(0.0f)
        , m_front(0.0f, 0.0f, -1.0f)
        , m_up(0.0f, 1.0f, 0.0f)
        , m_right(1.0f, 0.0f, 0.0f)
        , m_worldUp(0.0f, 1.0f, 0.0f)
        , m_yaw(-90.0f)
        , m_pitch(0.0f)
        , m_fov(fovDegrees)
        , m_near(nearPlane)
        , m_far(farPlane)
        , m_aspectRatio(16.0f / 9.0f)
        , m_viewDirty(true)
    {
        updateVectors();
    }

    // ---- 矩阵获取 ----

    /** 获取 view 矩阵 */
    glm::mat4 getViewMatrix() const
    {
        if (m_viewDirty)
        {
            m_viewMatrix = glm::lookAt(m_position, m_position + m_front, m_up);
            m_viewDirty = false;
        }
        return m_viewMatrix;
    }

    /** 获取 projection 矩阵 */
    glm::mat4 getProjectionMatrix() const
    {
        return glm::perspective(glm::radians(m_fov), m_aspectRatio, m_near, m_far);
    }

    // ---- 位置与方向 ----

    void setPosition(const glm::vec3& pos)
    {
        m_position = pos;
        m_viewDirty = true;
    }

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getFront()    const { return m_front; }
    glm::vec3 getRight()    const { return m_right; }
    glm::vec3 getUp()       const { return m_up; }

    float getNear() const { return m_near; }
    float getFar()  const { return m_far; }

    // ---- 视角控制 ----

    /**
     * 通过鼠标移动旋转视角
     * @param xoffset 水平偏移量
     * @param yoffset 垂直偏移量
     * @param sensitivity 灵敏度
     */
    void rotate(float xoffset, float yoffset, float sensitivity = 0.1f)
    {
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        m_yaw   += xoffset;
        m_pitch -= yoffset; // 鼠标上移 → 俯视（pitch 减小）

        // 限制俯仰角，防止万向锁
        if (m_pitch > 89.0f)  m_pitch = 89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;

        m_viewDirty = true;
        updateVectors();
    }

    /** 设置视野 */
    void setFov(float fovDegrees)
    {
        m_fov = fovDegrees;
    }

    /** 设置宽高比 */
    void setAspectRatio(float aspect)
    {
        m_aspectRatio = aspect;
    }

    // ---- 视锥体数据（用于剔除） ----

    /** 获取视锥体的 6 个平面（用于 CPU 端视锥体剔除） */
    struct FrustumPlane
    {
        glm::vec3 normal;
        float distance; // 平面方程: normal · point = distance
    };

    /**
     * 提取视锥体 6 个平面（左、右、下、上、近、远）
     * 使用 Gribb-Hartmann 方法从 View-Projection 矩阵提取
     *
     * 平面方程: normal · point = distance
     * 点在平面内侧（可见侧）的条件: normal · point >= distance
     *
     * 提取公式:
     *   Left:   row3 + row0
     *   Right:  row3 - row0
     *   Bottom: row3 + row1
     *   Top:    row3 - row1
     *   Near:   row3 + row2
     *   Far:    row3 - row2
     */
    std::array<FrustumPlane, 6> getFrustumPlanes() const
    {
        glm::mat4 vp = getProjectionMatrix() * getViewMatrix();
        std::array<FrustumPlane, 6> planes;

        // 获取 VP 矩阵的 4 行
        glm::vec4 row0 = glm::row(vp, 0);
        glm::vec4 row1 = glm::row(vp, 1);
        glm::vec4 row2 = glm::row(vp, 2);
        glm::vec4 row3 = glm::row(vp, 3);

        // 左、右、下、上、近、远
        struct PlaneDef { glm::vec4 base; glm::vec4 offset; float sign; };
        PlaneDef defs[6] = {
            {row3, row0,  1.0f},  // 左:  row3 + row0
            {row3, row0, -1.0f},  // 右:  row3 - row0
            {row3, row1,  1.0f},  // 下:  row3 + row1
            {row3, row1, -1.0f},  // 上:  row3 - row1
            {row3, row2,  1.0f},  // 近:  row3 + row2
            {row3, row2, -1.0f}   // 远:  row3 - row2
        };

        for (int i = 0; i < 6; ++i)
        {
            // Gribb-Hartmann 公式: plane = row3 + sign * rowI
            // 平面方程: plane.x*x + plane.y*y + plane.z*z + plane.w = 0
            // 内部点满足: plane · p >= 0
            // 转换为 normal · p = distance 格式:
            //   normal = vec3(plane) / len
            //   distance = -plane.w / len
            // 内部点满足: dot(normal, p) >= distance
            glm::vec4 plane = defs[i].base + defs[i].sign * defs[i].offset;
            float len = glm::length(glm::vec3(plane));
            if (len > 0.0f)
            {
                planes[i].normal   = glm::vec3(plane) / len;
                planes[i].distance = -plane.w / len; // 注意负号！
            }
            else
            {
                // 安全回退：不应该发生，但防止除零
                planes[i].normal   = glm::vec3(0.0f, 1.0f, 0.0f);
                planes[i].distance = 0.0f;
            }
        }

        return planes;
    }

private:
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    float m_yaw;     // 偏航角（水平旋转）
    float m_pitch;   // 俯仰角（垂直旋转）
    float m_fov;     // 视野角度
    float m_near;    // 近裁剪面
    float m_far;     // 远裁剪面
    float m_aspectRatio;

    mutable glm::mat4 m_viewMatrix;
    mutable bool m_viewDirty;

    /** 根据欧拉角更新方向向量 */
    void updateVectors()
    {
        glm::vec3 front;
        front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
        front.y = std::sin(glm::radians(m_pitch));
        front.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
        m_front = glm::normalize(front);
        m_right = glm::normalize(glm::cross(m_front, m_worldUp));
        m_up    = glm::normalize(glm::cross(m_right, m_front));
    }
};

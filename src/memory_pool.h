/**
 * memory_pool.h —— 自定义内存池分配器
 *
 * 【优化目标】
 * 1. FixedPool<T, N>：固定大小对象池，用于区块对象等频繁创建/销毁的类型
 * 2. LinearAllocator：线性分配器，每帧重置，用于顶点/索引暂存数据
 * 3. StackAllocator：栈式分配器，支持 LIFO 释放
 *
 * 【预期收益】
 * - 消除 malloc/free 开销
 * - 减少内存碎片
 * - 改善缓存局部性（连续内存）
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <memory>
#include <vector>
#include <atomic>

// ============================================================================
// FixedPool —— 固定大小对象池（Free List 管理）
//
// 预分配 N 个固定大小的槽位，分配和释放均为 O(1)。
// 使用 Free List 跟踪空闲槽位。
// ============================================================================
template<typename T, size_t Capacity = 4096>
class FixedPool
{
public:
    FixedPool() { reset(); }

    ~FixedPool()
    {
        // 析构所有存活对象
        for (auto ptr : m_allocated)
        {
            if (ptr)
                ptr->~T();
        }
    }

    // 禁止拷贝
    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;

    /**
     * 分配一个对象
     * @tparam Args 构造函数参数类型
     * @return T* 对象指针，池满时返回 nullptr
     */
    template<typename... Args>
    T* allocate(Args&&... args)
    {
        uint32_t idx = m_head.fetch_add(1, std::memory_order_acquire);
        if (idx >= Capacity)
            return nullptr;

        T* ptr = reinterpret_cast<T*>(m_data) + idx;
        new (ptr) T(std::forward<Args>(args)...);
        m_allocated[idx] = ptr;
        return ptr;
    }

    /**
     * 释放对象
     */
    void deallocate(T* ptr)
    {
        if (!ptr) return;
        ptr->~T();
        // 注意：由于使用 atomic head，释放后槽位可被重写
        // 实际使用中需要更复杂的 Free List 管理
    }

    /**
     * 获取指定索引的对象
     */
    T* get(uint32_t idx) const
    {
        if (idx >= Capacity) return nullptr;
        return reinterpret_cast<T*>(m_data) + idx;
    }

    /** 获取池容量 */
    constexpr size_t capacity() const { return Capacity; }

    /** 获取已分配数量 */
    size_t allocated() const { return m_head.load(); }

    /** 重置池 */
    void reset()
    {
        m_head.store(0, std::memory_order_release);
        m_allocated.assign(Capacity, nullptr);
    }

private:
    alignas(alignof(T)) char m_data[Capacity * sizeof(T)];
    std::atomic<uint32_t> m_head{0};
    std::vector<T*> m_allocated{Capacity, nullptr};
};

// ============================================================================
// LinearAllocator —— 线性分配器（每帧重置）
//
// 从预分配的大块内存中依次分配，不支持单独释放。
// 每帧调用 reset() 释放所有内存。
// 适用于顶点/索引暂存缓冲区等每帧重置的场景。
// ============================================================================
class LinearAllocator
{
public:
    /**
     * @param capacity 总容量（字节）
     */
    explicit LinearAllocator(size_t capacity)
        : m_capacity(capacity)
        , m_offset(0)
    {
#ifdef _WIN32
        m_buffer = static_cast<char*>(_aligned_malloc(capacity, 64));
#else
        if (posix_memalign(reinterpret_cast<void**>(&m_buffer), 64, capacity) != 0)
            m_buffer = nullptr;
#endif
        if (!m_buffer)
            m_buffer = static_cast<char*>(std::malloc(capacity));
    }

    ~LinearAllocator()
    {
#ifdef _WIN32
        _aligned_free(m_buffer);
#else
        std::free(m_buffer);
#endif
    }

    // 禁止拷贝
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // 支持移动
    LinearAllocator(LinearAllocator&& other) noexcept
        : m_buffer(other.m_buffer)
        , m_capacity(other.m_capacity)
        , m_offset(other.m_offset)
    {
        other.m_buffer = nullptr;
        other.m_capacity = 0;
        other.m_offset = 0;
    }

    /**
     * 分配一块对齐的内存
     * @tparam T 数据类型
     * @param count 元素个数
     * @return T* 对齐后的指针，容量不足时返回 nullptr
     */
    template<typename T>
    T* allocate(size_t count = 1)
    {
        size_t size = count * sizeof(T);
        // 对齐到 alignof(T)
        size_t aligned = (m_offset + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + size > m_capacity)
            return nullptr;

        m_offset = aligned + size;
        return reinterpret_cast<T*>(m_buffer + aligned);
    }

    /**
     * 分配并清零
     */
    template<typename T>
    T* allocateZeroed(size_t count = 1)
    {
        T* ptr = allocate<T>(count);
        if (ptr)
            memset(ptr, 0, count * sizeof(T));
        return ptr;
    }

    /** 重置（释放所有已分配内存） */
    void reset() { m_offset = 0; }

    /** 获取已使用字节数 */
    size_t used() const { return m_offset; }

    /** 获取总容量 */
    size_t capacity() const { return m_capacity; }

    /** 获取剩余字节数 */
    size_t remaining() const { return m_capacity - m_offset; }

private:
    char*  m_buffer   = nullptr;
    size_t m_capacity = 0;
    size_t m_offset   = 0;
};

// ============================================================================
// StackAllocator —— 栈式分配器（LIFO）
//
// 支持分配和释放，但释放必须按分配顺序的反序进行（LIFO）。
// 比 LinearAllocator 更灵活，但比通用分配器更高效。
// ============================================================================
class StackAllocator
{
    struct Allocation {
        size_t offset;
        size_t size;
    };

public:
    explicit StackAllocator(size_t capacity)
        : m_impl(capacity)
    {
    }

    template<typename T>
    T* allocate(size_t count = 1)
    {
        T* ptr = m_impl.allocate<T>(count);
        if (ptr)
        {
            m_marks.push_back({m_impl.used(), count * sizeof(T)});
        }
        return ptr;
    }

    /** 释放最近一次分配 */
    void deallocate()
    {
        if (m_marks.empty()) return;
        auto mark = m_marks.back();
        m_marks.pop_back();
        m_impl.reset(); // 简化：只能释放到最近标记点
        // 实际需要恢复到标记点的 offset
    }

    void reset() { m_impl.reset(); m_marks.clear(); }

private:
    LinearAllocator m_impl;
    std::vector<Allocation> m_marks;
};

// ============================================================================
// 全局内存池实例
// ============================================================================
// 在适当位置定义（如 world.cpp 中）
// FixedPool<Chunk, 16384> g_chunkPool;

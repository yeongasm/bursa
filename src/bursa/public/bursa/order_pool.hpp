#pragma once
#ifndef BURSA_ORDER_POOL_HPP
#define BURSA_ORDER_POOL_HPP

#include <vector>

#include "types.hpp"

namespace bursa
{
class OrderPool : public non_copyable
{
public:
    struct Node
    {
        Order order;
        size_t previous;    // Index of previous order.
        size_t next;        // Index of next order.
    };

    OrderPool(size_t defaultCapacity, std::pmr::polymorphic_allocator<Order> const& allocator = std::pmr::polymorphic_allocator<Order>{}) :
        nodes(allocator),
        freeList(allocator)
    {
        nodes.reserve(defaultCapacity);
        freeList.reserve(defaultCapacity);
    }

    template <typename... Args>
    auto emplace(Args&&... args) -> size_t requires std::constructible_from<Order, Args...>
    {
        if (!freeList.empty())
        {
            auto idx = freeList.back();
            freeList.pop_back();

            auto& node = nodes[idx];

            new (&node.order) Order{ std::forward<Args>(args)... };

            node.previous  = npos;
            node.next      = npos;

            return idx;
        }

        nodes.emplace_back(Order{ std::forward<Args>(args)... }, npos, npos);
        return nodes.size() - size_t{ 1 };
    }

    auto erase(size_t idx) -> void
    {
        freeList.push_back(idx);
    }

    template <typename Self>
    auto at(this Self&& self, size_t idx) -> auto&&
    {
        return std::forward_like<Self>(self.nodes[idx]);
    }

    auto size() const -> size_t
    {
        return nodes.size() - freeList.size();
    }
private:
    template <typename T>
    using container_type = std::vector<T, std::pmr::polymorphic_allocator<T>>;

    container_type<Node>    nodes;
    container_type<size_t>  freeList;
};
}

#endif // !BURSA_ORDER_POOL_HPP
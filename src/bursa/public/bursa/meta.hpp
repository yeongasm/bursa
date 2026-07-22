#pragma once
#ifndef BURSA_TYPES_HPP
#define BURSA_TYPES_HPP

#include <type_traits>

using u8    = unsigned char;
using u16   = unsigned short;
using u32   = unsigned int;
using u64   = unsigned long long;
using i8    = char;
using i16   = short;
using i32   = int;
using i64   = long long;
using f32   = float;
using f64   = double;

namespace bursa
{
struct non_copyable
{
    non_copyable() = default;

    non_copyable(non_copyable&&) = default;
    auto operator=(non_copyable&&) -> non_copyable& = default;
};

enum class order_id : u32 {};

template <template <typename...> typename TemplatedType, typename What>
struct _instance_of : std::false_type {};

template <template <typename...> typename What, typename... Ts>
struct _instance_of<What, What<Ts...>> : std::true_type {};

template <typename T, template <typename...> typename TemplatedType>
concept instance_of = _instance_of<TemplatedType, T>::value;

template <typename>
struct instrument_of
{
    using type = void;
    static_assert(false, "Supplied object does not have an instrument.");
};

template <typename Environment>
requires requires { typename Environment::instrument_type; }
struct instrument_of<Environment>
{
    using type = typename Environment::instrument_type;
};

template <typename Self>
using instrument_of_t = typename instrument_of<Self>::type;

template <typename>
struct value_type_of
{
    using type = void;
    static_assert(false, "Value type is not set!");
};

template <typename Environment>
requires requires { typename Environment::value_type; }
struct value_type_of<Environment>
{
    using type = typename Environment::value_type;
};

template <typename Environment>
using value_type_of_t = typename value_type_of<Environment>::type;
}

#endif // !BURSA_TYPES_HPP
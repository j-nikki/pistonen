#pragma once

#include <chrono>
#include <concepts>
#include <string.h>

#include "jutil.h"

//! @brief formatting related functions
//!
//! Usage example:
//!
//!     auto sz   = format::maxsz("hello, employee nr. ", 42, "!");
//!     auto buf  = std::make_unique_for_overwrite<char[]>(sz);
//!     auto last = format::format(buf.get(), "hello, employee nr. ", 42, "!");
//!
namespace format
{
namespace sc = std::chrono;
struct hdr_time {
};

namespace detail
{
constexpr char strs[]{'S', 'u', 'n', 'M', 'o', 'n', 'T', 'u', 'e', 'W', 'e', 'd', 'T', 'h', 'u',
                      'F', 'r', 'i', 'S', 'a', 't', 'J', 'a', 'n', 'F', 'e', 'b', 'M', 'a', 'r',
                      'A', 'p', 'r', 'M', 'a', 'y', 'J', 'u', 'n', 'J', 'u', 'l', 'A', 'u', 'g',
                      'S', 'e', 'p', 'O', 'c', 't', 'N', 'o', 'v', 'D', 'e', 'c'};
template <std::size_t N>
constexpr auto os = ([]<std::size_t... Is>(std::index_sequence<Is...>)->std::array<const char, N> {
    return {(Is, '0')...};
})(std::make_index_sequence<N>{});
} // namespace detail
#define FMT_STR(Idx) reinterpret_cast<const char(*)[3]>(&detail::strs[(Idx)*3])

template <std::size_t N, std::integral T>
struct fmt_width_ty {
    T x;
};
template <std::size_t N, class T>
constexpr JUTIL_INLINE fmt_width_ty<N, T> fmt_width(T &&x) noexcept
{
    return {static_cast<T &&>(x)};
}

#define FMT_TIMESTAMP(DIdx, DD, MIdx, YYYY, H, M, S)                                               \
    FMT_STR(DIdx), ", ", fmt_width<2>(DD), " ", FMT_STR((MIdx) + 7), " ", fmt_width<4>(YYYY), " ", \
        fmt_width<2>(H), ":", fmt_width<2>(M), ":", fmt_width<2>(S), " GMT"

//
// formatter
//

template <class T>
struct formatter;

// clang-format off
template <class T>
concept custom_formatable = requires(char *d_f, const T &x) {
    { format::formatter<std::remove_cvref_t<T>>::format(d_f, x) } noexcept -> std::same_as<char *>;
    { format::formatter<std::remove_cvref_t<T>>::maxsz(x) } noexcept -> std::same_as<std::size_t>;
};
template <class T>
concept lazyarg = requires(const T &x, const char *p) {
    { x.size() } noexcept -> std::same_as<std::size_t>;
    { x.write(p) } noexcept -> std::same_as<char *>;
};
// clang-format on

template <lazyarg T>
struct formatter<T> {
    static char *format(const char *d_f, const T &x) noexcept { return x.write(d_f); }
    static std::size_t maxsz(const T &x) noexcept { return x.size(); }
};

//
// format
//

namespace detail
{
struct format_impl {
    static JUTIL_INLINE char *format(char *const d_f) noexcept { return d_f; }

    //
    // string formatting
    //

    template <std::size_t N, class... Rest>
    static JUTIL_INLINE char *format(char *const d_f, const char (&x)[N], Rest &&...rest) noexcept
    {
        memcpy(d_f, x, N - 1);
        return format_impl ::format(d_f + (N - 1), static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class... Rest>
    static JUTIL_INLINE char *format(char *const d_f, const char (*const x)[N],
                                     Rest &&...rest) noexcept
    {
        memcpy(d_f, x, N);
        return format_impl::format(d_f + N, static_cast<Rest &&>(rest)...);
    }

    template <class... Rest>
    static JUTIL_INLINE char *format(char *const d_f, const std::string_view x,
                                     Rest &&...rest) noexcept
    {
        memcpy(d_f, x.data(), x.size());
        return format_impl::format(d_f + x.size(), static_cast<Rest &&>(rest)...);
    }

    //
    // integer formatting
    //

    static const char radix_100_table[200];
    static char *itoa(const uint32_t x, char *d_f) noexcept;

    template <std::integral T, class... Rest>
    static JUTIL_INLINE char *format(char *d_f, T x, Rest &&...rest) noexcept
    {
        if constexpr (std::is_signed_v<T>) {
            if (x < 0) {
                x      = -x;
                *d_f++ = '-';
            }
        }
        return format_impl::format(format_impl::itoa(static_cast<uint32_t>(x), d_f),
                                   static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class T, class... Rest>
    static JUTIL_INLINE char *format(char *d_f, fmt_width_ty<N, T> x, Rest &&...rest) noexcept
    {
        static_assert(N > 0 && N <= 10, "unsupported width");

        [[maybe_unused]] const bool neg = x.x < 0;
        if constexpr (std::is_signed_v<T>)
            if (neg)
                x.x = -x.x;

        // algorithm taken from: https://jk-jeon.github.io/posts/2022/02/jeaiii-algorithm/
        auto y = ((x.x * uint64_t{720575941}) >> 24) + 1;
#define EAT_NUM(M)                                                                                 \
    if constexpr (N == M + 1)                                                                      \
        d_f[N - M - 1] = radix_100_table[int(y >> 32) * 2 + 1];                                    \
    else if constexpr (N > M + 1)                                                                  \
        memcpy(d_f + (N - M - 2), radix_100_table + int(y >> 32) * 2, 2);                          \
    y = static_cast<uint32_t>(y) * uint64_t{100};
        EAT_NUM(8);
        EAT_NUM(6);
        EAT_NUM(4);
        EAT_NUM(2);
        EAT_NUM(0);
#undef EAT_NUM

        if constexpr (std::is_signed_v<T>)
            if (neg)
                *d_f = '-';

        return format_impl::format(d_f + N, static_cast<Rest &&>(rest)...);
    }

    //
    // timestamp formatting
    //

    static JUTIL_NOINLINE char *format_timestamp(char *const d_f) noexcept;

    template <class... Rest>
    static JUTIL_INLINE char *format(char *const d_f, hdr_time, Rest &&...rest) noexcept
    {
        return format_impl::format(format_timestamp(d_f), static_cast<Rest &&>(rest)...);
    }

    //
    // user-provided formatting
    //

    template <custom_formatable T, class... Rest>
    static JUTIL_INLINE char *format(char *const d_f, const T &x, Rest &&...rest) noexcept
    {
        return format_impl::format(format::formatter<std::remove_cvref_t<T>>::format(d_f, x),
                                   static_cast<Rest &&>(rest)...);
    }
};
} // namespace detail
template <class... Ts>
constexpr JUTIL_INLINE char *format(char *const d_f, Ts &&...xs) noexcept
{
    return detail::format_impl::format(d_f, static_cast<Ts &&>(xs)...);
}

//
// format_maxsz
//

namespace detail
{
struct maxsz_impl {
    static constexpr JUTIL_INLINE std::size_t maxsz() noexcept { return 0; }

    //
    // string maxsz
    //

    template <std::size_t N, class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(const char (&)[N], Rest &&...rest) noexcept
    {
        return N - 1 + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(const char (*)[N], Rest &&...rest) noexcept
    {
        return N + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(std::string_view sv, Rest &&...rest) noexcept
    {
        return sv.size() + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // integer maxsz
    //

    template <std::integral T, class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(T, Rest &&...rest) noexcept
    {
        return 10 + std::is_signed_v<T> + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class T, class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(fmt_width_ty<N, T>, Rest &&...rest) noexcept
    {
        return N + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // timestamp maxsz
    //

    template <class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(hdr_time, Rest &&...rest) noexcept
    {
        return maxsz_impl::maxsz(FMT_TIMESTAMP(0, 0, 0, 0, 0, 0, 0), static_cast<Rest &&>(rest)...);
    }

    //
    // user-provided maxsz
    //

    template <custom_formatable T, class... Rest>
    static constexpr JUTIL_INLINE std::size_t maxsz(const T &x, Rest &&...rest) noexcept
    {
        return format::formatter<std::remove_cvref_t<T>>::maxsz(x) +
               maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }
};
} // namespace detail
template <class... Ts>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t maxsz(Ts &&...xs) noexcept
{
    return detail::maxsz_impl::maxsz(static_cast<Ts &&>(xs)...);
}

// clang-format off
template <class T>
concept formatable = requires(char *d_f, const T &x) {
    detail::format_impl::format(d_f, x);
};
// clang-format on
} // namespace format
#pragma once

#include <chrono>
#include <concepts>
#include <emmintrin.h>
#include <ranges>
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
namespace sr = std::ranges;
struct hdr_time {};

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

//
// fx
//

template <char...>
struct cs {
};
template <class, class, class>
struct fx_ty;
template <char... Cs0, char... Cs1, class T>
struct fx_ty<cs<Cs0...>, cs<Cs1...>, T> {
    static constexpr std::size_t ncs = sizeof...(Cs0) + sizeof...(Cs1);
    static constexpr std::array cs0{Cs0...};
    static constexpr std::array cs1{Cs1...};
    T x;
};
} // namespace detail

inline namespace fx
{
template <class T>
JUTIL_CI auto bold(T &&x) noexcept
    -> detail::fx_ty<detail::cs<'\033', '[', '1', 'm'>, detail::cs<'\033', '[', '2', '2', 'm'>, T>
{
    return {static_cast<T &&>(x)};
}
template <char... Cs, class T>
JUTIL_CI auto fg(T &&x, detail::cs<Cs...> = {}) noexcept
    -> detail::fx_ty<detail::cs<'\033', '[', '3', '8', ';', '5', ';', Cs..., 'm'>,
                     detail::cs<'\033', '[', '3', '9', 'm'>, T>
{
    return {static_cast<T &&>(x)};
}
template <char... Cs, class T>
JUTIL_CI auto bg(T &&x, detail::cs<Cs...> = {}) noexcept
    -> detail::fx_ty<detail::cs<'\033', '[', '4', '8', ';', '5', ';', Cs..., 'm'>,
                     detail::cs<'\033', '[', '4', '9', 'm'>, T>
{
    return {static_cast<T &&>(x)};
}
constexpr inline detail::cs<'0'> black;
constexpr inline detail::cs<'1'> red;
constexpr inline detail::cs<'2'> green;
constexpr inline detail::cs<'3'> yellow;
constexpr inline detail::cs<'4'> blue;
constexpr inline detail::cs<'5'> magenta;
constexpr inline detail::cs<'6'> cyan;
constexpr inline detail::cs<'7'> white;
constexpr inline detail::cs<'8'> bright_black;
constexpr inline detail::cs<'9'> bright_red;
constexpr inline detail::cs<'1', '0'> bright_green;
constexpr inline detail::cs<'1', '1'> bright_yellow;
constexpr inline detail::cs<'1', '2'> bright_blue;
constexpr inline detail::cs<'1', '3'> bright_magenta;
constexpr inline detail::cs<'1', '4'> bright_cyan;
constexpr inline detail::cs<'1', '5'> bright_white;
} // namespace fx

//
// hex
//

struct hex_ty {
    uint64_t x; // TODO: support more widths
};
JUTIL_CI hex_ty hex(const uint64_t x) noexcept { return {x}; }

//
// fmt_width
//

template <std::size_t N, class T>
struct fmt_width_ty {
    T x;
};
template <std::size_t N, std::integral T>
JUTIL_CI fmt_width_ty<N, T> fmt_width(const T x) noexcept
{
    return {x};
}

#define FMT_STR(Idx) reinterpret_cast<const char(*)[3]>(&detail::strs[(Idx)*3])
#define FMT_TIMESTAMP(DIdx, DD, MIdx, YYYY, H, M, S)                                               \
    FMT_STR(DIdx), ", ", fmt_width<2>(DD), " ", FMT_STR((MIdx) + 7), " ", fmt_width<4>(YYYY), " ", \
        fmt_width<2>(H), ":", fmt_width<2>(M), ":", fmt_width<2>(S), " GMT"

//
// custom_formatable
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
concept lazyarg = requires(const T &x, char *p) {
    { x.size() } noexcept -> std::same_as<std::size_t>;
    { x.write(p) } noexcept -> std::same_as<char *>;
};
// clang-format on

//
// maxsz
//

namespace detail
{
struct maxsz_impl {
    static JUTIL_CI std::size_t maxsz() noexcept { return 0; }

    //
    // string maxsz
    //

    template <std::size_t N, class... Rest>
    static JUTIL_CI std::size_t maxsz(const char (&)[N], Rest &&...rest) noexcept
    {
        return N - 1 + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class... Rest>
    static JUTIL_CI std::size_t maxsz(const char (*)[N], Rest &&...rest) noexcept
    {
        return N + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <class... Rest>
    static JUTIL_CI std::size_t maxsz(std::string_view sv, Rest &&...rest) noexcept
    {
        return sv.size() + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // char maxsz
    //

    template <class... Rest>
    static JUTIL_CI std::size_t maxsz(char, Rest &&...rest) noexcept
    {
        return 1 + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // bool maxsz
    //

    // template <class... Rest>
    // static JUTIL_CI std::size_t maxsz(bool, Rest &&...rest) noexcept
    // {
    //     return 5 + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    // }

    //
    // hex maxsz
    //

    template <class... Rest>
    static JUTIL_CI std::size_t maxsz(const hex_ty &, Rest &&...rest) noexcept
    {
        return 16 + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // integer maxsz
    //

    template <std::integral T, class... Rest>
    static JUTIL_CI std::size_t maxsz(T, Rest &&...rest) noexcept
    {
        return 10 + std::is_signed_v<T> + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class T, class... Rest>
    static JUTIL_CI std::size_t maxsz(fmt_width_ty<N, T> x, Rest &&...rest) noexcept
    {
        return (x.x < 0) + N + maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }

    //
    // fx maxsz
    //

    template <jutil::instance_of<fx_ty> T, class... Rest>
    static JUTIL_CI std::size_t maxsz(T &&fx, Rest &&...rest) noexcept
    {
        return std::remove_cvref_t<T>::ncs + maxsz_impl::maxsz(fx.x, static_cast<Rest &&>(rest)...);
    }

    //
    // timestamp maxsz
    //

    template <class... Rest>
    static JUTIL_CI std::size_t maxsz(hdr_time, Rest &&...rest) noexcept
    {
        return maxsz_impl::maxsz(FMT_TIMESTAMP(0, 0, 0, 0, 0, 0, 0), static_cast<Rest &&>(rest)...);
    }

    //
    // tuple maxsz
    //

    template <jutil::instance_of<std::tuple> T, class... Rest>
    static JUTIL_CI std::size_t maxsz(T &&xs_, Rest &&...rest_) noexcept
    {
        return []<std::size_t... Is>(T && xs, std::index_sequence<Is...>, Rest && ...rest)
        {
            return maxsz_impl::maxsz(std::get<Is>(static_cast<T &&>(xs))...,
                                     static_cast<Rest &&>(rest)...);
        }
        (static_cast<T &&>(xs_),
         std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{},
         static_cast<Rest &&>(rest_)...);
    }

    //
    // user-provided maxsz
    //

    template <custom_formatable T, class... Rest>
    static JUTIL_CI std::size_t maxsz(const T &x, Rest &&...rest) noexcept
    {
        return format::formatter<std::remove_cvref_t<T>>::maxsz(x) +
               maxsz_impl::maxsz(static_cast<Rest &&>(rest)...);
    }
};
} // namespace detail

template <class... Ts>
[[nodiscard]] JUTIL_CI std::size_t maxsz(Ts &&...xs) noexcept
{
    return detail::maxsz_impl::maxsz(static_cast<Ts &&>(xs)...);
}

//
// formatter
//

template <lazyarg T>
struct formatter<T> {
    static char *format(char *d_f, const T &x) noexcept { return x.write(d_f); }
    static std::size_t maxsz(const T &x) noexcept { return x.size(); }
};

//
// format
//

namespace detail
{
extern const __m128i radix;
extern const __m128i pi8los;
struct format_impl {
    static JUTIL_CI char *format(char *const d_f) noexcept { return d_f; }

    //
    // string formatting
    //

    template <std::size_t N, class... Rest>
    static JUTIL_CI char *format(char *const d_f, const char (&x)[N], Rest &&...rest) noexcept
    {
        return format_impl::format(std::copy_n(x, N - 1, d_f), static_cast<Rest &&>(rest)...);
    }

    template <std::size_t N, class... Rest>
    static JUTIL_CI char *format(char *const d_f, const char (*const x)[N], Rest &&...rest) noexcept
    {
        return format_impl::format(std::copy_n(*x, N - 1, d_f), static_cast<Rest &&>(rest)...);
    }

    template <class... Rest>
    static JUTIL_CI char *format(char *const d_f, const std::string_view x, Rest &&...rest) noexcept
    {
        return format_impl::format(sr::copy(x, d_f).out, static_cast<Rest &&>(rest)...);
    }

    //
    // bool formatting
    //

    // template <class... Rest>
    // static JUTIL_INLINE char *format(char *const d_f, const bool x, Rest &&...rest) noexcept
    // {
    //     static constexpr std::string_view false_ = "false";
    //     static constexpr std::string_view true_  = "true";
    //     return format_impl::format(d_f, x ? true_ : false_, static_cast<Rest &&>(rest)...);
    // }

    //
    // char formatting
    //

    template <class... Rest>
    static JUTIL_CI char *format(char *const d_f, const char x, Rest &&...rest) noexcept
    {
        *d_f = x;
        return format_impl::format(d_f + 1, static_cast<Rest &&>(rest)...);
    }

    //
    // fx formatting
    //

    template <jutil::instance_of<fx_ty> T, class... Rest>
    static JUTIL_CI char *format(char *const d_f, T &&fx, Rest &&...rest) noexcept
    {
        return format_impl::format(d_f, fx.cs0, fx.x, fx.cs1, static_cast<Rest &&>(rest)...);
    }

    //
    // hex formatting
    //

    template <class... Rest>
    static JUTIL_CI char *format(char *d_f, const hex_ty x_, Rest &&...rest) noexcept
    {
        JUTIL_IF_CONSTEVAL {
            static constexpr char radix_[]{'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
            for (const auto x : std::bit_cast<std::array<uint8_t, 8>>(x_.x))
                *d_f++ = radix_[x >> 4], *d_f++ = radix_[x & 0xf];
        } else {
            const auto lo = x_.x, hi = (x_.x >> 4);
            auto xs = _mm_unpacklo_epi8(_mm_cvtsi64_si128(lo), _mm_cvtsi64_si128(hi));
            xs      = _mm_and_si128(xs, pi8los);
            xs      = _mm_shuffle_epi8(radix, xs);
            _mm_storeu_si128(static_cast<__m128i *>(static_cast<void *>(d_f)), xs);
        }
        return format_impl::format(d_f + 16, static_cast<Rest &&>(rest)...);
    }

    //
    // integer formatting
    //

    static const char radix_100_table[200];
    static char *itoa(const uint32_t x, char *d_f) noexcept;

    template <std::integral T, class... Rest>
    static JUTIL_CI char *format(char *d_f, T x, Rest &&...rest) noexcept
    {
        if constexpr (std::is_signed_v<T>)
            if (x < 0) x = -x, *d_f++ = '-';

        JUTIL_IF_CONSTEVAL {
            char buf[maxsz_impl::maxsz(T{})], *it = buf;
            do {
                *it++ = '0' + (x % 10);
            } while ((x /= 10));
            return format_impl::format(std::copy(buf, it, d_f), static_cast<Rest &&>(rest)...);
        } else {
            return format_impl::format(format_impl::itoa(static_cast<uint32_t>(x), d_f),
                                       static_cast<Rest &&>(rest)...);
        }
    }

    template <std::size_t N, class T, class... Rest>
    static JUTIL_INLINE char *format(char *d_f, fmt_width_ty<N, T> x, Rest &&...rest) noexcept
    {
        static_assert(N > 0 && N <= 10, "unsupported width");

        [[maybe_unused]] const bool neg = x.x < 0;
        if constexpr (std::is_signed_v<T>)
            if (neg) x.x = -x.x;

        // algorithm taken from: https://jk-jeon.github.io/posts/2022/02/jeaiii-algorithm/
        auto y = ((x.x * uint64_t{720575941}) >> 24) + 1;
#define EAT_NUM(M)                                                                                 \
    if constexpr (N == M + 1)                                                                      \
        d_f[N - M - 1] = radix_100_table[int(y >> 32) * 2 + 1];                                    \
    else if constexpr (N > M + 1)                                                                  \
        memcpy(d_f + (N - M - 2), radix_100_table + int(y >> 32) * 2, 2);                          \
    y *= 100;
        EAT_NUM(8);
        EAT_NUM(6);
        EAT_NUM(4);
        EAT_NUM(2);
        EAT_NUM(0);
#undef EAT_NUM

        if constexpr (std::is_signed_v<T>)
            if (neg) *d_f = '-';

        return format_impl::format(d_f + N, static_cast<Rest &&>(rest)...);
    }

    //
    // timestamp formatting
    //

    static JUTIL_NOINLINE char *format_timestamp(char *const d_f) noexcept;

    template <class... Rest>
    static JUTIL_CI char *format(char *const d_f, hdr_time, Rest &&...rest) noexcept
    {
        return format_impl::format(format_timestamp(d_f), static_cast<Rest &&>(rest)...);
    }

    //
    // tuple formatting
    //

    template <jutil::instance_of<std::tuple> T, class... Rest>
    static JUTIL_CI char *format(char *const d_f, T &&xs_, Rest &&...rest_) noexcept
    {
        return [=]<std::size_t... Is>(T && xs, std::index_sequence<Is...>, Rest && ...rest)
        {
            return format_impl::format(d_f, std::get<Is>(static_cast<T &&>(xs))...,
                                       static_cast<Rest &&>(rest)...);
        }
        (static_cast<T &&>(xs_),
         std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{},
         static_cast<Rest &&>(rest_)...);
    }

    //
    // user-provided formatting
    //

    template <custom_formatable T, class... Rest>
    static JUTIL_CI char *format(char *const d_f, const T &x, Rest &&...rest) noexcept
    {
        return format_impl::format(format::formatter<std::remove_cvref_t<T>>::format(d_f, x),
                                   static_cast<Rest &&>(rest)...);
    }
};
} // namespace detail
template <class... Ts>
JUTIL_CI char *format(char *const d_f, Ts &&...xs) noexcept
{
    return detail::format_impl::format(d_f, static_cast<Ts &&>(xs)...);
}

// clang-format off
template <class T>
concept formatable = requires(char *d_f, const T &x) {
    detail::format_impl::format(d_f, x);
};
// clang-format on
} // namespace format

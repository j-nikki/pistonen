#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/comparison/equal.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <cassert>
#include <concepts>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <type_traits>
#include <x86intrin.h>

#include "lmacro_begin.h"

namespace jutil
{
#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#else
// 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size  = 64;
#endif

namespace sr = std::ranges;
namespace sv = std::views;

namespace impl
{
struct deferty {
    template <class F>
    constexpr auto operator=(F &&f) noexcept
    {
        struct R {
            F f;
            ~R() { f(); }
        };
        return R{static_cast<F &&>(f)};
    }
};
} // namespace impl

template <class To, class From>
concept static_castable = requires(From &&x)
{
    static_cast<To>(static_cast<From &&>(x));
};
template <class To, class From>
concept bit_castable = sizeof(To) == sizeof(From) &&
                       std::is_trivially_copyable_v<To> &&std::is_trivially_copyable_v<From>;
template <class To, class From>
concept opaque_castable = static_castable<To, From> || bit_castable<To, From>;
template <class To, class From>
constexpr To opaque_cast(From &&x) noexcept
{
    if constexpr (static_castable<To, From>)
        return static_cast<To>(static_cast<From &&>(x));
    else
        return std::bit_cast<To>(static_cast<From &&>(x));
}
template <class To, opaque_castable<To> From>
constexpr To opaque_cast(From &x) noexcept
{
    if constexpr (static_castable<To, From>)
        return static_cast<To>(x);
    else
        return std::bit_cast<To>(x);
}

#define JUTIL_cat_exp(X, Y) X##Y
#define CAT(X, Y)           JUTIL_cat_exp(X, Y)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5104)
#endif
#define JUTIL_wstr_exp(X) L#X
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define WSTRINGIFY(X) JUTIL_wstr_exp(X)
#define DEFER         const auto CAT(__defer_, __LINE__) = impl::deferty{} =

#ifdef _MSC_VER
#define JUTIL_PREFETCH(P) (void)0
#define JUTIL_INLINE      inline __forceinline
#define JUTIL_NOINLINE    __declspec(noinline)
#define JUTIL_TRAP()      __debugbreak()
#define JUTIL_NO_DEFAULT()                                                                         \
    default:                                                                                       \
        __assume(0)
#else
#define JUTIL_PREFETCH(P)                                                                          \
    do {                                                                                           \
        if (!std::is_constant_evaluated())                                                         \
            __builtin_prefetch(P);                                                                 \
    } while (0)
#define JUTIL_INLINE           inline __attribute__((always_inline))
#define JUTIL_NOINLINE         __attribute__((noinline))
#define JUTIL_pd_exp(D1, ...)  D1 __VA_OPT__(JUTIL_pd_exp(__VA_ARGS__))
#define JUTIL_PUSH_DIAG(...)   _Pragma("GCC diagnostic push") JUTIL_pd_exp(__VA_ARGS__)
#define JUTIL_POP_DIAG()       _Pragma("GCC diagnostic pop")
#define JUTIL_WNO_UNUSED_VALUE _Pragma("GCC diagnostic ignored \"-Wunused-value\"")
#define JUTIL_WNO_UNUSED_PARAM _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")
#define JUTIL_WNO_SHADOW       _Pragma("GCC diagnostic ignored \"-Wshadow\"")
#define JUTIL_WNO_PARENTHESES  _Pragma("GCC diagnostic ignored \"-Wparentheses\"")
#define JUTIL_WNO_SEQUENCE     _Pragma("GCC diagnostic ignored \"-Wsequence-point\"")
#define JUTIL_WNO_CCAST        _Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#define JUTIL_TRAP()           __builtin_trap()
#define JUTIL_NO_DEFAULT()                                                                         \
    default:                                                                                       \
        __builtin_unreachable()
#endif

#ifndef NDEBUG
#define DBGEXPR(...)  __VA_ARGS__
#define DBGSTMNT(...) __VA_ARGS__
#define JUTIL_c_impl(E, For, ...)                                                                  \
    [&]<class BOOST_PP_CAT(JaT, __LINE__)>(                                                        \
        BOOST_PP_CAT(JaT, __LINE__) && BOOST_PP_CAT(jae, __LINE__)) -> BOOST_PP_CAT(JaT,           \
                                                                                    __LINE__) {    \
        if (!(BOOST_PP_CAT(jae, __LINE__) For)) {                                                  \
            if constexpr (jutil::opaque_castable<uintmax_t, BOOST_PP_CAT(JaT, __LINE__)>)          \
                fprintf(stderr, "\033[2m" __FILE__ ":" BOOST_PP_STRINGIZE(__LINE__) ":\033[0m assertion failed: \033[1m%s "              \
                                              "\033[2m(%#jx)\033[0;1m %s\033[0m\n",                \
                                    (__VA_OPT__((void)) #E __VA_OPT__(, __VA_ARGS__)),             \
                                    jutil::opaque_cast<uintmax_t>(BOOST_PP_CAT(jae, __LINE__)),    \
                                    #For);                                                         \
            else                                                                                   \
                fprintf(stderr, "\033[2m" __FILE__ ":" BOOST_PP_STRINGIZE(__LINE__) ":\033[0m assertion failed: \033[1m%s %s\033[0m\n",  \
                                    (__VA_OPT__((void)) #E __VA_OPT__(, __VA_ARGS__)), #For);      \
            JUTIL_TRAP();                                                                          \
        }                                                                                          \
        return BOOST_PP_CAT(jae, __LINE__);                                                        \
    }(E)
#else
#define DBGEXPR(...) ((void)0)
#define DBGSTMNT(...)
#define JUTIL_c_impl(E, ...)                                                                       \
    [&]<class BOOST_PP_CAT(JaT, __LINE__)>(                                                        \
        BOOST_PP_CAT(JaT, __LINE__) && BOOST_PP_CAT(jae, __LINE__)) -> BOOST_PP_CAT(JaT,           \
                                                                                    __LINE__) {    \
        return BOOST_PP_CAT(jae, __LINE__);                                                        \
    }(E)
#endif

#define JUTIL_FAIL(Msg)                                                                            \
    ([]<bool BOOST_PP_CAT(F, __LINE__) = false>() {                                                \
        static_assert(BOOST_PP_CAT(F, __LINE__), Msg);                                             \
    })()
#define JUTIL_c_exp_fold_op(d, acc, x) JUTIL_c_impl(acc, x, BOOST_PP_CAT(msg, __LINE__))
#define JUTIL_c_exp_fold(E, For0, ...)                                                             \
    [&]<class BOOST_PP_CAT(JaeT, __LINE__)>(BOOST_PP_CAT(JaeT, __LINE__) &&                        \
                                            BOOST_PP_CAT(e, __LINE__)) -> BOOST_PP_CAT(JaeT,       \
                                                                                       __LINE__) { \
        JUTIL_PUSH_DIAG(JUTIL_WNO_SHADOW);                                                         \
        static constexpr auto BOOST_PP_CAT(msg, __LINE__) = #E;                                    \
        return BOOST_PP_SEQ_FOLD_LEFT(                                                             \
            JUTIL_c_exp_fold_op,                                                                   \
            JUTIL_c_impl(BOOST_PP_CAT(e, __LINE__), For0, BOOST_PP_CAT(msg, __LINE__)),            \
            BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__));                                                \
        JUTIL_POP_DIAG()                                                                           \
    }(E)
#define JUTIL_c_exp(E, For0, ...)                                                                  \
    BOOST_PP_IF(BOOST_PP_CHECK_EMPTY(__VA_ARGS__), JUTIL_c_impl, JUTIL_c_exp_fold)                 \
    (E, For0, __VA_ARGS__)
#define CHECK(E, ...)                                                                              \
    BOOST_PP_IF(BOOST_PP_CHECK_EMPTY(__VA_ARGS__), JUTIL_c_impl, JUTIL_c_exp)(E, __VA_ARGS__)

//
// callable traits (function call expression only)
//
// clang-format off
template <class T, class... Args>
concept callable = requires(T f) {
    f(std::declval<Args>()...);
};
template <class T, class R, class... Args>
concept callable_r = requires(T f) {
    { f(std::declval<Args>()...) } -> std::convertible_to<R>;
};
// clang-format on
template <class T, class... Args>
concept predicate = callable_r<T, bool, Args...>;
template <class T, class... Args>
constexpr inline bool is_callable = callable<T, Args...>;
template <class R, class T, class... Args>
constexpr inline bool is_callable_r = callable_r<T, R, Args...>;
template <class T, class... Args>
using call_result = decltype(std::declval<T>()(std::declval<Args>()...));

//
// curry
//
template <class, class, class...>
struct curry;
template <class F, std::size_t... Is, class... Ts>
struct [[nodiscard]] curry<F, std::index_sequence<Is...>, Ts...> {
    F f_;
    std::tuple<Ts...> xs_;

    template <class... Us>
    requires(is_callable<F, Ts &&..., Us &&...>) constexpr JUTIL_INLINE auto
    operator()(Us &&...ys) noexcept(noexcept(f_(std::get<Is>(std::move(xs_))...,
                                                static_cast<Us &&>(ys)...)))
        -> decltype(f_(std::get<Is>(std::move(xs_))..., static_cast<Us &&>(ys)...))
    {
        return f_(std::get<Is>(std::move(xs_))..., static_cast<Us &&>(ys)...);
    }

    template <class... Us>
    requires(is_callable<F, const Ts &..., Us &&...>) constexpr JUTIL_INLINE auto
    operator()(Us &&...ys) const
        noexcept(noexcept(f_(std::get<Is>(xs_)..., static_cast<Us &&>(ys)...)))
            -> decltype(f_(std::get<Is>(xs_)..., static_cast<Us &&>(ys)...))
    {
        return f_(std::get<Is>(xs_)..., static_cast<Us &&>(ys)...);
    }

    template <class... Us>
    requires(!is_callable<F, Ts &&..., Us &&...>) constexpr JUTIL_INLINE
        curry<F, std::index_sequence_for<Ts..., Us...>, Ts..., Us...>
        operator()(Us &&...ys) noexcept(
            noexcept(curry<F, std::index_sequence_for<Ts..., Us...>, Ts..., Us...>{
                std::move(f_), {std::get<Is>(std::move(xs_))..., static_cast<Us &&>(ys)...}}))
    {
        return {std::move(f_), {std::get<Is>(std::move(xs_))..., static_cast<Us &&>(ys)...}};
    }

    template <class... Us>
    requires(!is_callable<F, const Ts &..., Us &&...>) constexpr JUTIL_INLINE
        curry<F, std::index_sequence_for<Ts..., Us...>, Ts..., Us...>
        operator()(Us &&...ys) const
        noexcept(noexcept(curry<F, std::index_sequence_for<Ts..., Us...>, Ts..., Us...>{
            std::move(f_), {std::get<Is>(xs_)..., static_cast<Us &&>(ys)...}}))
    {
        return {std::move(f_), {std::get<Is>(xs_)..., static_cast<Us &&>(ys)...}};
    }
};
template <class F>
curry(F &&) -> curry<F, std::index_sequence<>>;

//
// slice
//
constexpr inline auto npos = std::numeric_limits<std::ptrdiff_t>::max();
template <std::ptrdiff_t I, std::ptrdiff_t J, std::size_t N>
constexpr inline auto slc_a = J == npos ? 0 : I;
template <std::ptrdiff_t I, std::ptrdiff_t J, std::size_t N>
constexpr inline auto slc_b = J == npos ? (I < 0 ? N + I : I) : (J < 0 ? N + J : J);
template <std::ptrdiff_t I, std::ptrdiff_t J, class T, std::size_t N>
using slcty = std::span<T, slc_b<I, J, N> - slc_a<I, J, N>>;
template <std::ptrdiff_t I, std::ptrdiff_t J = npos, class T, std::size_t N>
constexpr JUTIL_INLINE slcty<I, J, const T, N> slice(const std::array<T, N> &arr) noexcept
{
    return slcty<I, J, const T, N>{&arr[slc_a<I, J, N>], &arr[slc_b<I, J, N>]};
}
template <std::ptrdiff_t I, std::ptrdiff_t J = npos, class T, std::size_t N>
constexpr JUTIL_INLINE slcty<I, J, T, N> slice(std::array<T, N> &arr) noexcept
{
    return slcty<I, J, T, N>{&arr[slc_a<I, J, N>], &arr[slc_b<I, J, N>]};
}

constexpr JUTIL_INLINE std::size_t operator""_uz(unsigned long long int x) noexcept
{
    return static_cast<std::size_t>(x);
}

template <class>
struct is_sized_span : std::false_type {
};
template <class T, std::size_t N>
struct is_sized_span<std::span<T, N>> : std::bool_constant<N != std::dynamic_extent> {
};

template <class T>
concept constant_sized = (std::is_bounded_array_v<T> || is_sized_span<T>::value ||
                          requires { std::tuple_size<T>::value; });
template <class T>
concept constant_sized_input_range = sr::input_range<T> && constant_sized<std::remove_cvref_t<T>>;
template <class T>
concept borrowed_constant_sized_range =
    sr::borrowed_range<T> && constant_sized<std::remove_cvref_t<T>>;
template <class T>
concept borrowed_input_range = sr::borrowed_range<T> && sr::input_range<T>;
template <class T>
concept sized_input_range = sr::input_range<T> &&(requires(T r) { sr::size(r); });

template <class T_>
constexpr auto csr_sz() noexcept
{
    using T = std::remove_cvref_t<T_>;
    if constexpr (std::is_bounded_array_v<T>)
        return []<class T, std::size_t N>(std::type_identity<T[N]>) {
            return std::integral_constant<std::size_t, N>{};
        }(std::type_identity<T>{});
    else if constexpr (is_sized_span<T>::value)
        return []<class T, std::size_t N>(std::type_identity<std::span<T, N>>) {
            return std::integral_constant<std::size_t, N>{};
        }(std::type_identity<T>{});
    else
        return std::tuple_size<T>{};
}

static_assert(constant_sized_input_range<const std::array<int, 1> &>);
static_assert(constant_sized_input_range<const std::span<int, 1> &>);
static_assert(constant_sized_input_range<const int (&)[5]>);

template <sr::random_access_range R, class Comp, class Proj>
requires(std::sortable<sr::iterator_t<R>, Comp, Proj> &&requires(R r) {
    sr::size(r);
}) constexpr JUTIL_INLINE sr::iterator_t<R> sort_until_snt(R &&r, Comp comp, Proj proj, auto pred)
{
    const auto l = sr::next(sr::begin(r), sr::size(r) - 1);
    for (auto it = sr::begin(r);; ++it, assert(it != sr::end(r)))
        if (sr::nth_element(it, it, l, comp, proj); pred(*it))
            return it;
}
template <sr::random_access_range R, class Comp, class Proj, class Pred, class Snt>
requires(std::sortable<sr::iterator_t<R>, Comp, Proj> &&requires(R r) {
    sr::size(r);
}) constexpr JUTIL_INLINE sr::iterator_t<R> sort_until_snt(R &&r, Comp comp, Proj proj, Pred pred,
                                                           Snt &&snt)
{
    *sr::next(sr::begin(r), sr::size(r) - 1) = static_cast<Snt &&>(snt);
    return sort_until_snt(r, comp, proj, pred, snt);
}

template <std::size_t NTypes, class TCnt = std::size_t, sr::input_range R,
          class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE std::array<TCnt, NTypes>
counts(R &&r, Proj proj = {}) noexcept(noexcept(proj(*sr::begin(r))))
{
    std::array<TCnt, NTypes> cnts{};
    for (auto &x : r)
        ++cnts[proj(x)];
    return cnts;
}

//
// find_unrl
//
namespace impl
{
template <std::size_t I>
constexpr JUTIL_INLINE auto find_if_unrl(auto &r, auto it,
                                         auto pred) noexcept(noexcept(pred(*sr::next(it))))
{
    if constexpr (!I)
        return sr::end(r);
    else if (pred(*it))
        return it;
    else
        return find_if_unrl<I - 1>(r, sr::next(it), pred);
}
template <std::size_t I>
constexpr JUTIL_INLINE auto find_unrl(auto &r, auto it,
                                      const auto &x) noexcept(noexcept(*sr::next(it) == x))
{
    if constexpr (!I)
        return sr::end(r);
    else if (*it == x)
        return it;
    else
        return find_unrl<I - 1>(r, sr::next(it), x);
}
} // namespace impl
template <borrowed_constant_sized_range R, class Pred>
[[nodiscard]] constexpr JUTIL_INLINE sr::iterator_t<R>
find_if_unrl(R &&r,
             Pred pred) noexcept(noexcept(impl::find_if_unrl<csr_sz<R>()>(r, sr::begin(r), pred)))
{
    return impl::find_if_unrl<csr_sz<R>()>(r, sr::begin(r), pred);
}
template <borrowed_constant_sized_range R>
[[nodiscard]] constexpr JUTIL_INLINE sr::iterator_t<R>
find_unrl(R &&r, const auto &x) noexcept(noexcept(impl::find_unrl<csr_sz<R>()>(r, sr::begin(r), x)))
{
    return impl::find_unrl<csr_sz<R>()>(r, sr::begin(r), x);
}
template <borrowed_constant_sized_range R>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t
find_unrl_idx(R &&r, const auto &x) noexcept(noexcept(sr::distance(sr::begin(r), find_unrl(r, x))))
{
    return static_cast<std::size_t>(sr::distance(sr::begin(r), find_unrl(r, x)));
}
template <borrowed_constant_sized_range R>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t
find_if_unrl_idx(R &&r,
                 auto pred) noexcept(noexcept(sr::distance(sr::begin(r), find_if_unrl(r, pred))))
{
    return static_cast<std::size_t>(sr::distance(sr::begin(r), find_if_unrl(r, pred)));
}

//
// map
//
template <std::size_t N, std::size_t Pad = 0, bool InitPad = true, sr::input_range R,
          callable<std::size_t, sr::range_reference_t<R>> F>
[[nodiscard]] constexpr JUTIL_INLINE auto map_n(R &&r, F f) noexcept(
    noexcept(std::decay_t<decltype(f(0_uz, *sr::begin(r)))>{f(0_uz, *sr::begin(r))}))
    -> std::array<std::decay_t<decltype(f(0_uz, *sr::begin(r)))>, N + Pad>
{
    using OEl = std::decay_t<decltype(f(0_uz, *sr::begin(r)))>;
    if (std::is_constant_evaluated()) {
        std::array<OEl, N + Pad> res{};
        auto i = 0_uz;
        for (sr::range_reference_t<R> x : static_cast<R &&>(r))
            res[i] = f(i, x), ++i;
        return res;
    } else {
        std::array<OEl, N + Pad> res;
        auto i = 0_uz;
        for (sr::range_reference_t<R> x : static_cast<R &&>(r))
            new (&res[i]) OEl{f(i, x)}, ++i;
        if constexpr (InitPad)
            std::uninitialized_default_construct_n(std::next(res.begin(), N), Pad);
        return res;
    }
}
template <std::size_t N, std::size_t Pad = 0, bool InitPad = true, sr::input_range R,
          callable<sr::range_reference_t<R>> F = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE auto
map_n(R &&r,
      F f = {}) noexcept(noexcept(std::decay_t<decltype(f(*sr::begin(r)))>{f(*sr::begin(r))}))
    -> std::array<std::decay_t<decltype(f(*sr::begin(r)))>, N + Pad>
{
    using OEl = std::decay_t<decltype(f(*sr::begin(r)))>;
    if (std::is_constant_evaluated()) {
        std::array<OEl, N + Pad> res{};
        auto i = 0_uz;
        for (sr::range_reference_t<R> x : static_cast<R &&>(r))
            res[i++] = f(x);
        return res;
    } else {
        std::array<OEl, N + Pad> res;
        auto i = 0_uz;
        for (sr::range_reference_t<R> x : static_cast<R &&>(r))
            new (&res[i++]) OEl{f(x)};
        if constexpr (InitPad)
            std::uninitialized_default_construct_n(std::next(res.begin(), N), Pad);
        return res;
    }
}
template <std::size_t Pad = 0, bool InitPad = true, constant_sized_input_range R>
[[nodiscard]] constexpr JUTIL_INLINE auto
map(R &&r, auto f) noexcept(noexcept(map_n<csr_sz<R>(), Pad, InitPad>(static_cast<R &&>(r), f)))
    -> decltype(map_n<csr_sz<R>(), Pad, InitPad>(static_cast<R &&>(r), f))
{
    return map_n<csr_sz<R>(), Pad, InitPad>(static_cast<R &&>(r), f);
}

static_assert(std::is_same_v<decltype(map(std::array{0}, L(x + 1.))), std::array<double, 1>>);
static_assert(std::is_same_v<decltype(map<1>(std::array{0}, L(x + 1))), std::array<int, 2>>);

template <sr::range R, class InitT = sr::range_value_t<R>>
[[nodiscard]] constexpr InitT sum(R &&r, InitT init = {}) noexcept
{
    for (const auto &x : static_cast<R &&>(r))
        init += x;
    return init;
}

template <class T>
std::make_unsigned_t<T> to_unsigned(T x)
{
    return static_cast<std::make_unsigned_t<T>>(x);
}

constexpr JUTIL_INLINE uintptr_t to_uint(void *x) noexcept { return std::bit_cast<uintptr_t>(x); }
constexpr JUTIL_INLINE void *to_ptr(uintptr_t x) noexcept { return std::bit_cast<void *>(x); }

//
// Duff's device
//

template <std::size_t Factor = 16>
constexpr JUTIL_INLINE void duffs(std::integral auto n, callable auto &&f) noexcept(noexcept(f()))
{
    if (!n)
        return;
#define JUTIL_dd_c(z, j, fact)                                                                     \
    case (j ? fact - j : 0):                                                                       \
        f();                                                                                       \
        BOOST_PP_IF(BOOST_PP_EQUAL(j, BOOST_PP_DEC(fact)), break, [[fallthrough]]);
    if constexpr (Factor == 1) {
        while (n--)
            f();
    }
#define JUTIL_dd_fact(z, _, fact)                                                                  \
    else if constexpr (Factor == fact)                                                             \
    {                                                                                              \
        switch (auto i = (n + fact - 1) / fact; n % fact) {                                        \
            do {                                                                                   \
                BOOST_PP_REPEAT(fact, JUTIL_dd_c, fact)                                            \
                JUTIL_NO_DEFAULT();                                                                \
            } while (--i);                                                                         \
        }                                                                                          \
    }
    BOOST_PP_SEQ_FOR_EACH(JUTIL_dd_fact, ~, (2)(4)(8)(16)(32)(64))
    else JUTIL_FAIL("Factor must be one of: 1,2,4,8,16,32,64");
#undef JUTIL_dd_fact
#undef JUTIL_dd_c
}

//
// intrinsics
//
inline namespace intrin
{
#define JUTIL_i_enum(r, _, i, x)     BOOST_PP_COMMA_IF(i) x BOOST_PP_CAT(arg, i)
#define JUTIL_i_enum_fwd(r, _, i, x) BOOST_PP_COMMA_IF(i) BOOST_PP_CAT(arg, i)
#define JUTIL_intrin_exp(n, in, s)                                                                 \
    auto n(BOOST_PP_SEQ_FOR_EACH_I(JUTIL_i_enum, ~, s)) noexcept(                                  \
        noexcept(in(BOOST_PP_SEQ_FOR_EACH_I(JUTIL_i_enum_fwd, ~, s))))                             \
        ->decltype(in(BOOST_PP_SEQ_FOR_EACH_I(JUTIL_i_enum_fwd, ~, s)))                            \
    {                                                                                              \
        return in(BOOST_PP_SEQ_FOR_EACH_I(JUTIL_i_enum_fwd, ~, s));                                \
    }
#define JUTIL_intrin(name, iname, ...)                                                             \
    JUTIL_intrin_exp(name, iname, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))
// clang-format off
JUTIL_intrin(ffs, __builtin_ffs, int)
JUTIL_intrin(clz, __builtin_clz, unsigned int)
JUTIL_intrin(ctz, __builtin_ctz, unsigned int)
JUTIL_intrin(clrsb, __builtin_clrsb, int)
JUTIL_intrin(popcount, __builtin_popcount, unsigned int)
JUTIL_intrin(parity, __builtin_parity, unsigned int)
JUTIL_intrin(ffs, __builtin_ffsl, long)
JUTIL_intrin(clz, __builtin_clzl, unsigned long)
JUTIL_intrin(ctz, __builtin_ctzl, unsigned long)
JUTIL_intrin(clrsb, __builtin_clrsbl, long)
JUTIL_intrin(popcount, __builtin_popcountl, unsigned long)
JUTIL_intrin(parity, __builtin_parityl, unsigned long)
JUTIL_intrin(ffs, __builtin_ffsll, long long)
JUTIL_intrin(clz, __builtin_clzll, unsigned long long)
JUTIL_intrin(ctz, __builtin_ctzll, unsigned long long)
JUTIL_intrin(clrsb, __builtin_clrsbll, long long)
JUTIL_intrin(popcount, __builtin_popcountll, unsigned long long)
JUTIL_intrin(parity, __builtin_parityll, unsigned long long)
JUTIL_intrin(powi, __builtin_powi, double, int)
JUTIL_intrin(powi, __builtin_powif, float, int)
JUTIL_intrin(powi, __builtin_powil, long double, int)
JUTIL_intrin(bswap, __builtin_bswap16, uint16_t)
JUTIL_intrin(bswap, __builtin_bswap32, uint32_t)
JUTIL_intrin(bswap, __builtin_bswap64, uint64_t)
JUTIL_intrin(bsr, 63 ^ __builtin_clzl, unsigned long)
JUTIL_intrin(bsr, 31 ^ __builtin_clz, unsigned int)
// clang-format on
} // namespace intrin

//
// call
//
template <callable<> F>
constexpr call_result<F &>
call_until(F &&f, std::predicate<call_result<F &>> auto pr) noexcept(noexcept(pr(f())))
{
    for (;;)
        if (decltype(auto) res = f(); pr(res))
            return static_cast<decltype(res)>(res);
}
template <callable<> F>
constexpr call_result<F &>
call_while(F &&f, std::predicate<call_result<F &>> auto pr) noexcept(noexcept(pr(f())))
{
    for (;;)
        if (decltype(auto) res = f(); !pr(res))
            return static_cast<decltype(res)>(res);
}

//
// transform_while
//
template <std::input_iterator It, class UnaryOp,
          std::output_iterator<call_result<UnaryOp &, std::iter_reference_t<It>>> It2,
          std::predicate<std::iter_reference_t<It>> Pred>
constexpr It2
transform_while(It f, It l, It2 f2, UnaryOp transf,
                Pred pred) noexcept(noexcept(*++f2 = transf(*++f)) &&noexcept(pred(*f)))
{
    for (; f != l && pred(*f); ++f, ++f2)
        *f2 = transf(*f);
    return f2;
}

//
// transform_until
//
template <std::random_access_iterator It, class UnaryOp,
          std::output_iterator<call_result<UnaryOp &, std::iter_reference_t<It>>> It2>
constexpr It2 transform_until_snt(It f, const It l, It2 f2, UnaryOp transf, const auto &x) noexcept(
    noexcept(*++f2 = transf(*++f)) &&noexcept(f[l - f - 1] = x))
{
    f[l - f - 1] = x;
    for (; *f != x; ++f, ++f2)
        *f2 = transf(*f);
    return f2;
}
template <std::input_iterator It, class UnaryOp,
          std::output_iterator<call_result<UnaryOp &, std::iter_reference_t<It>>> It2>
constexpr It2 transform_always_until(
    It f, const It l, It2 f2, UnaryOp transf,
    const auto &x) noexcept(noexcept(*++f2 = transf(*++f)) &&noexcept(f[l - f - 1] = x))
{
    for (; *f != x; ++f, ++f2)
        CHECK(f != l), *f2 = transf(*f);
    return f2;
}

//
// has
//
template <sr::input_range R, class TVal, class Proj = std::identity>
[[nodiscard]] constexpr bool has(R &&r, const TVal &val, Proj proj = {})
{
    return sr::find(r, val, proj) != sr::end(r);
}
template <constant_sized_input_range R, class TVal, class Proj = std::identity>
[[nodiscard]] constexpr bool has_unrl(R &&r, const TVal &val, Proj proj = {})
{
    return find_unrl(r, val) != sr::end(r);
}

//
// swap
//
struct iter_swap_t {
    constexpr JUTIL_INLINE void operator()(auto a, auto b) const
        noexcept(noexcept(sr::iter_swap(a, b)))
    {
        sr::iter_swap(a, b);
    }
};
constexpr inline iter_swap_t iter_swap{};
constexpr inline curry mirror_swap{
    [](auto src, auto dst, auto a, auto b) noexcept(noexcept(
        sr::iter_swap(sr::next(src, sr::distance(dst, a)), sr::next(src, sr::distance(dst, b))))) {
        const auto aoff = sr::distance(dst, a);
        const auto boff = sr::distance(dst, b);
        sr::iter_swap(sr::next(src, aoff), sr::next(src, boff));
    }};

//
// max heap
//
template <std::random_access_iterator I, std::sentinel_for<I> S, class Comp = sr::less,
          class Proj = std::identity, class IterSwap = iter_swap_t>
requires std::sortable<I, Comp, Proj>
constexpr I
push_heap(I f, S l, Comp comp = {}, Proj proj = {},
          IterSwap is = {}) noexcept(noexcept(comp(proj(f[0]), proj(f[0]))) &&noexcept(is(f, f)))
{
    auto xi = (l - f) - 1;
    for (auto upi = xi / 2;; (xi = upi, upi = xi / 2))
        if (!xi || comp(proj(f[xi]), proj(f[upi])))
            return f + xi;
        else
            is(f + upi, f + xi);
}

//
// eytzinger-indexed heap
//
template <class IdxTy = std::size_t, std::random_access_iterator I, std::sentinel_for<I> S,
          class Comp = sr::less, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE bool
is_heap_eytz(I f, S l, Comp comp = {},
             Proj proj = {}) noexcept(noexcept(comp(proj(f[0]), proj(f[0]))))
{
    const auto n = static_cast<IdxTy>(l - f);
    for (IdxTy i = 1; i / 2 < n; ++i) {
        const auto li = i * 2, ri = i * 2 + 1;
        if (li < n && !comp(proj(f[li]), proj(f[i])))
            return false;
        if (ri < n && !comp(proj(f[i]), proj(f[ri])))
            return false;
    }
    return true;
}
template <class RetTy = std::size_t, bool Trim = true, std::random_access_iterator I,
          std::sentinel_for<I> S, class Comp = sr::less, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE RetTy lower_bound_eytz_idx(
    I f, S l, const auto &x, Comp comp = {}, Proj proj = {}) noexcept(noexcept(comp(proj(f[1]), x)))
{
    CHECK(is_heap_eytz(f, l, comp, proj));
    const auto n = static_cast<RetTy>(l - f);
    RetTy i      = 1;
    while (i < n) {
        JUTIL_PREFETCH(f + i * (hardware_destructive_interference_size / sizeof(f[i])));
        i = i * 2 + comp(proj(f[i]), x);
    }
    return Trim ? CHECK(i >> __builtin_ffs(~i), > 0, < n) : i;
}
template <class IdxTy = std::size_t, std::random_access_iterator I, std::sentinel_for<I> S,
          class Comp = sr::less, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE I
lower_bound_eytz(I f, S l, const auto &x, Comp comp = {},
                 Proj proj = {}) noexcept(noexcept(comp(proj(f[1]), proj(f[1]))))
{
    return f + lower_bound_eytz_idx<IdxTy>(f, l, x, comp, proj);
}
JUTIL_PUSH_DIAG(JUTIL_WNO_PARENTHESES, JUTIL_WNO_SEQUENCE)
template <class IdxTy = std::size_t, std::size_t DD = 1, std::random_access_iterator I,
          std::sentinel_for<I> S, class Comp = sr::less, class Proj = std::identity>
constexpr JUTIL_INLINE void
push_eytz(I f, S l, Comp comp = {}, Proj proj = {}) noexcept(noexcept(comp(proj(f[1]), proj(f[1]))))
{
    const auto xi = static_cast<IdxTy>(l - f) - 1;
    if (xi == 1)
        return;
    const auto hm1         = static_cast<IdxTy>(bsr(to_unsigned(xi - 1)));
    const IdxTy ctzxormsk1 = ~(2u << hm1), ctzxormsk2 = ~(1u << hm1);
    for (;;) {
        CHECK(is_heap_eytz(f, f + xi, comp, proj));
        IdxTy lb = 1;
        duffs<DD>(hm1, [&] { lb = (lb * 2) + comp(proj(f[lb]), proj(f[xi])); });
        const auto lblt = lb < xi;
        lb              = !lblt ? lb : lb * 2 + comp(proj(f[lb]), proj(f[xi]));
        if (lb == xi)
            break;
        const auto ctzxormsk = lblt ? ctzxormsk1 : ctzxormsk2;
        const auto nz        = static_cast<IdxTy>(ctz(to_unsigned((lb & 1) ? lb ^ ctzxormsk : lb)));
        std::iter_value_t<I> tmp{sr::iter_move(f + (lb >> nz))};
        duffs<DD>(nz - 1, [&, i = nz] mutable { f[lb >> i--] = sr::iter_move(f + (lb >> i - 1)); });
        f[lb >> 1] = sr::iter_move(f + xi);
        f[xi]      = std::move(tmp);
    }
}
JUTIL_POP_DIAG()

//
// find_always
//
template <sr::random_access_range R, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, sr::range_reference_t<R>>>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t find_always_idx(R &r, const T &x, Proj proj = {})
{
    const auto it = sr::begin(r);
    for (std::size_t i = 0;; ++i, assert(i != sr::size(r)))
        if (proj(it[i]) == x)
            return i;
}
template <std::input_iterator It, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, std::iter_reference_t<It>>>
[[nodiscard]] constexpr JUTIL_INLINE It find_always(It f, [[maybe_unused]] const It l, const T &x,
                                                    Proj proj = {})
{
    while (proj(*f) != x)
        CHECK(f != l), ++f;
    return f;
}
template <sr::input_range R, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, sr::range_reference_t<R>>>
[[nodiscard]] constexpr JUTIL_INLINE sr::iterator_t<R> find_always(R &r, const T &x, Proj proj = {})
{
    return find_always(sr::begin(r), x, proj);
}
template <std::random_access_iterator It, class Pred, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t
find_if_always_idx(It f, [[maybe_unused]] const It l, Pred pred, Proj proj = {})
{
    for (std::size_t i = 0;; ++i)
        if (CHECK(i != static_cast<std::size_t>(sr::distance(f, l))), pred(proj(f[i])))
            return i;
}
template <sr::random_access_range R, class Pred, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t find_if_always_idx(R &r, Pred pred, Proj proj = {})
{
    return find_if_always_idx(sr::begin(r), sr::end(r), pred, proj);
}
template <std::input_iterator It, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE It find_if_always(It f, [[maybe_unused]] const It l, auto pred,
                                                       Proj proj = {})
{
    while (!pred(proj(*f)))
        CHECK(f != l), ++f;
    return f;
}
template <borrowed_input_range R, class Proj = std::identity>
[[nodiscard]] constexpr JUTIL_INLINE sr::iterator_t<R> find_if_always(R &&r, auto pred,
                                                                      Proj proj = {})
{
    return find_if_always(sr::begin(r), sr::end(r), pred, proj);
}

//
// find_snt
//
template <std::random_access_iterator It, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, std::iter_reference_t<It>>>
[[nodiscard]] constexpr JUTIL_INLINE It find_snt(It f, const It l, const T &x, Proj proj = {})
{
    proj(f[l - f - 1]) = x;
    while (proj(*f) != x)
        ++f;
    return f;
}
template <std::random_access_iterator It, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, std::iter_reference_t<It>>>
[[nodiscard]] constexpr JUTIL_INLINE It find_if_snt(It f, const It l, auto pred, const T &x,
                                                    Proj proj = {})
{
    proj(f[l - f - 1]) = x;
    while (!pred(proj(*f)))
        ++f;
    return f;
}
template <sr::random_access_range R, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, sr::range_reference_t<R>>>
[[nodiscard]] constexpr JUTIL_INLINE sr::iterator_t<R &> find_snt(R &r, const T &x, Proj proj = {})
{
    return find_snt(sr::begin(r), sr::end(r), x, proj);
}
template <sr::random_access_range R, class T, class Proj = std::identity>
requires std::is_lvalue_reference_v<call_result<Proj &, sr::range_reference_t<R>>>
[[nodiscard]] constexpr JUTIL_INLINE std::size_t find_snt_idx(R &r, const T &x, Proj proj = {})
{
    proj(sr::begin(r)[sr::size(r) - 1]) = x;
    return find_always_idx(r, x, proj);
}

template <std::size_t I>
struct getter {
    template <class T>
    [[nodiscard]] constexpr JUTIL_INLINE auto operator()(T &&x) const
        noexcept(noexcept(std::get<I>(static_cast<T &&>(x))))
            -> decltype(std::get<I>(static_cast<T &&>(x)))
    {
        return std::get<I>(static_cast<T &&>(x));
    }
};

constexpr inline getter<0> fst{};
constexpr inline getter<1> snd{};
constexpr inline auto abs_ = L(x < 0 ? -x : x);

#define FREF(F, ...)                                                                               \
    []<class... CAT(Ts, __LINE__)>(CAT(Ts, __LINE__) && ...CAT(xs, __LINE__)) noexcept(            \
        noexcept(F(                                                                                \
            __VA_ARGS__ __VA_OPT__(, ) static_cast<CAT(Ts, __LINE__) &&>(CAT(xs, __LINE__))...)))  \
        ->decltype(F(                                                                              \
            __VA_ARGS__ __VA_OPT__(, ) static_cast<CAT(Ts, __LINE__) &&>(CAT(xs, __LINE__))...))   \
    {                                                                                              \
        return F(                                                                                  \
            __VA_ARGS__ __VA_OPT__(, ) static_cast<CAT(Ts, __LINE__) &&>(CAT(xs, __LINE__))...);   \
    }

template <class InitT, sr::input_range R, class F>
requires(is_callable_r<InitT, F, InitT, sr::range_reference_t<R>>) [[nodiscard]] JUTIL_INLINE InitT
    fold(R &&r, InitT init, F f) noexcept(noexcept(init = f(init, *sr::begin(r))))
{
    for (sr::range_reference_t<R> x : r)
        init = f(init, x);
    return init;
}

template <auto A, auto B>
requires std::integral<decltype(A + B)>
constexpr inline auto iota = []<auto... xs>(std::integer_sequence<decltype(A + B), xs...>)
                                 -> std::array<decltype(A + B), sizeof...(xs)>
{
    return std::array{(A + xs)...};
}
(std::make_integer_sequence<decltype(A + B), B - A>{});

#define NO_COPY_MOVE(S)                                                                            \
    S(const S &) = delete;                                                                         \
    S(S &&)      = delete;                                                                         \
    S &operator=(const S &) = delete;                                                              \
    S &operator=(S &&) = delete
#define NO_DEF_CTORS(S)                                                                            \
    S()          = delete;                                                                         \
    S(const S &) = delete;                                                                         \
    S(S &&)      = delete;

} // namespace jutil

#include "lmacro_end.h"

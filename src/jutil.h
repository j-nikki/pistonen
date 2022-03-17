#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

#include "lmacro_begin.h"

namespace jutil
{

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

#define JUTIL_cat_exp(X, Y) X##Y
#define CAT(X, Y) JUTIL_cat_exp(X, Y)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5104)
#endif
#define JUTIL_wstr_exp(X) L#X
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define WSTRINGIFY(X) JUTIL_wstr_exp(X)
#define DEFER const auto CAT(__defer_, __LINE__) = impl::deferty{} =

// The qualifiee *must* be inlined; not being so is an error
#ifdef _MSC_VER
#define JUTIL_INLINE inline __forceinline
#define JUTIL_NOINLINE __declspec(noinline)
#else
#define JUTIL_INLINE inline __attribute__((always_inline))
#define JUTIL_NOINLINE __attribute__((noinline))
#endif

#ifndef NDEBUG
#define DBGEXPR(...) __VA_ARGS__
#define DBGSTMNT(...) __VA_ARGS__
#define JUTIL_assert(E, For, Str)                                                                  \
    [&] {                                                                                          \
        auto e = E;                                                                                \
        if (!(e For)) {                                                                            \
            fprintf(stderr, Str #E "\n");                                                          \
            assert(false);                                                                         \
        }                                                                                          \
        return e;                                                                                  \
    }()
#else
#define DBGEXPR(...) ((void)0)
#define DBGSTMNT(...)
#define JUTIL_assert(E, ...) E
#endif
#define ASSERT(E) JUTIL_assert(E, /**/, "Expression not true: ")
#define ASSERT_0(E) JUTIL_assert(E, == 0i64, "Expression non-zero: ")
#define ASSERT_EQ(E, X) JUTIL_assert(E, == X, "Expression not equal to " #X ":")
#define ASSERT_NEQ(E, X) JUTIL_assert(E, != X, "Expression not equal to " #X ":")

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
constexpr inline bool is_callable = callable<T, Args...>;
template <class R, class T, class... Args>
constexpr inline bool is_callable_r = callable_r<T, R, Args...>;
template <class T, class... Args>
using call_result = decltype(std::declval<T>()(std::declval<Args>()...));

namespace impl
{
template <class F, class G>
struct lty {
    F f_;
    G g_;
    template <class T>
    requires(requires { f_(std::declval<T &&>()); }) constexpr JUTIL_INLINE auto
    operator()(T &&x) noexcept(noexcept(f_(static_cast<T &&>(x))))
        -> decltype(f_(static_cast<T &&>(x)))
    {
        return f_(static_cast<T &&>(x));
    }
    template <class T>
    requires(requires { f_(std::declval<T &&>()); }) constexpr JUTIL_INLINE auto
    operator()(T &&x) const noexcept(noexcept(f_(static_cast<T &&>(x))))
        -> decltype(f_(static_cast<T &&>(x)))
    {
        return f_(static_cast<T &&>(x));
    }
    template <class T, class U>
    requires(requires { g_(std::declval<T &&>(), std::declval<U &&>()); }) constexpr JUTIL_INLINE
        auto
        operator()(T &&x, U &&y) noexcept(noexcept(g_(static_cast<T &&>(x), static_cast<U &&>(y))))
            -> decltype(f_(static_cast<T &&>(x), static_cast<U &&>(y)))
    {
        return g_(static_cast<T &&>(x), static_cast<U &&>(y));
    }
    template <class T, class U>
    requires(requires { g_(std::declval<T &&>(), std::declval<U &&>()); }) constexpr JUTIL_INLINE
        auto
        operator()(T &&x, U &&y) const
        noexcept(noexcept(g_(static_cast<T &&>(x), static_cast<U &&>(y))))
            -> decltype(f_(static_cast<T &&>(x), static_cast<U &&>(y)))
    {
        return g_(static_cast<T &&>(x), static_cast<U &&>(y));
    }
};
template <class F, class G>
lty(F &&, G &&) -> lty<F, G>;
}; // namespace impl

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
        ASSERT(f != l), *f2 = transf(*f);
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
        ASSERT(f != l), ++f;
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
        if (ASSERT(i != static_cast<std::size_t>(sr::distance(f, l))), pred(proj(f[i])))
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
        ASSERT(f != l), ++f;
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

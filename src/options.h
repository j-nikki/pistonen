#pragma once

#include <boost/variant2.hpp>
#include <ranges>
#include <string_view>

#include "jutil.h"

namespace options
{
using std::string_view;

namespace bv = boost::variant2;
namespace sr = std::ranges;

using arg_ty = bv::variant<std::string_view, int>;

// clang-format off
template <class T>
concept opt_visitor = requires(T &&f, string_view sv, arg_ty(arg)()) {
    { f(sv, arg) } -> jutil::one_of<int, void>;
};
template <class T>
concept arg_visitor = requires(T &&f, string_view sv) {
    { f(sv) } -> jutil::one_of<int, void>;
};
// clang-format on

namespace detail
{
template <class... Args>
JUTIL_CI int visit(auto &&v, Args &&...args) noexcept(noexcept(v(static_cast<Args &&>(args)...)))
{
    if constexpr (std::same_as<int, decltype(v(static_cast<Args &&>(args)...))>)
        return v(static_cast<Args &&>(args)...);
    return v(static_cast<Args &&>(args)...), 0;
}

struct visitor_fbdef {
    template <class... Args>
    JUTIL_CI int operator()(Args &&...) const noexcept
    {
        return 0; // ignore unused arguments
    }
};

template <std::size_t I>
using index = std::integral_constant<std::size_t, I>;

template <class, class>
struct vi_nothrow;
template <class V, std::size_t... Is>
struct vi_nothrow<V, std::index_sequence<Is...>>
    : std::bool_constant<(noexcept(std::declval<V &&>()(index<Is>{})) && ...)> {
};

template <std::size_t I = 0, class... Ts, class U, class V, class D>
JUTIL_CI int visit_idx(const std::tuple<Ts...> &prs, const U &y, V &&v,
                       D &&d) noexcept(vi_nothrow<V, std::index_sequence_for<Ts...>>::value)
{
    if constexpr (I == sizeof...(Ts))
        return static_cast<D &&>(d)();
    else if (std::get<I>(prs)(y))
        return static_cast<V &&>(v)(index<I>{});
    else
        return visit_idx<I + 1>(prs, y, static_cast<V &&>(v), static_cast<D &&>(d));
}

template <class... Ts, class U, std::size_t... Is>
JUTIL_CI std::tuple<Ts..., U> tpl_app(std::tuple<Ts...> &&tpl, U &&x,
                                      std::index_sequence<Is...>) noexcept
{
    return {std::get<Is>(static_cast<std::tuple<Ts...> &&>(tpl))..., static_cast<U &&>(x)};
}

template <class... Ts>
JUTIL_CI auto strs(Ts &&...xs_) noexcept
{
    return [xs = std::tuple{string_view{xs_}...}](const string_view sv) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>)
        {
            return ((std::get<Is>(xs) == sv) || ...);
        }
        (std::make_index_sequence<sizeof...(Ts)>{});
    };
}

constexpr inline auto select_visitor = [](auto &&v, const string_view sv, auto arg_) {
    return visit_idx(
        v.pfs_, sv, [&](auto I) { return visit(std::get<I>(v.fs_)); },
        [&] {
            if (const auto arg = arg_(); arg.index() == 0) {
                return visit_idx(
                    v.pss_, sv,
                    [&](auto J) { return visit(std::get<J>(v.ss_), bv::unsafe_get<0>(arg)); },
                    [&] { return v.unk_(sv); });
            } else
                return bv::unsafe_get<1>(arg);
        });
};

template <class Vs>
struct visitor_selector {
    Vs vs_;
    JUTIL_CI int operator()(const string_view sv, auto arg_) const //
        noexcept(noexcept(select_visitor(vs_, sv, arg_)))
    {
        return select_visitor(vs_, sv, arg_);
    }
    JUTIL_CI int operator()(const string_view sv, auto arg_) //
        noexcept(noexcept(select_visitor(vs_, sv, arg_)))
    {
        return select_visitor(vs_, sv, arg_);
    }
};

template <class, class, class, class, class>
struct visitor_maker;
template <class... PFs, class... PSs, class... Fs, class... Ss, class Unk>
struct visitor_maker<std::tuple<PFs...>, std::tuple<PSs...>, std::tuple<Fs...>, std::tuple<Ss...>,
                     Unk> {
    using this_type = visitor_maker<std::tuple<PFs...>, std::tuple<PSs...>, std::tuple<Fs...>,
                                    std::tuple<Ss...>, Unk>;
    std::tuple<PFs...> pfs_;
    std::tuple<PSs...> pss_;
    std::tuple<Fs...> fs_;
    std::tuple<Ss...> ss_;
    Unk unk_;
    JUTIL_CI auto operator()() &&noexcept -> visitor_selector<this_type>
    {
        return {static_cast<this_type &&>(*this)};
    }
    template <jutil::predicate<string_view> Pr, jutil::callable<string_view> V>
    JUTIL_CI auto operator()(Pr &&pr,
                             V &&v) && // TODO: noexcept(...)
        -> visitor_maker<std::tuple<PFs...>, std::tuple<PSs..., Pr>, std::tuple<Fs...>,
                         std::tuple<Ss..., V>, Unk>
    {
        return {static_cast<std::tuple<PFs...> &&>(pfs_),
                tpl_app(static_cast<std::tuple<PSs...> &&>(pss_), static_cast<Pr &&>(pr),
                        std::make_index_sequence<sizeof...(Ss)>{}),
                static_cast<std::tuple<Fs...> &&>(fs_),
                tpl_app(static_cast<std::tuple<Ss...> &&>(ss_), static_cast<V &&>(v),
                        std::make_index_sequence<sizeof...(Ss)>{}),
                static_cast<Unk &&>(unk_)};
    }
    template <jutil::callable<string_view> V>
    JUTIL_CI auto operator()(string_view sv, V &&v) &&noexcept(noexcept(static_cast<this_type &&> (
        *this)(strs(static_cast<string_view &&>(sv)), static_cast<V &&>(v))))
        -> decltype(static_cast<this_type &&>(*this)(strs(static_cast<string_view &&>(sv)),
                                                     static_cast<V &&>(v)))
    {
        return static_cast<this_type &&>(*this)(strs(static_cast<string_view &&>(sv)),
                                                static_cast<V &&>(v));
    }
    template <jutil::predicate<string_view> Pr, jutil::callable<> V>
    JUTIL_CI auto operator()(Pr &&pr,
                             V &&v) && // TODO: noexcept(...)
        -> visitor_maker<std::tuple<PFs..., Pr>, std::tuple<PSs...>, std::tuple<Fs..., V>,
                         std::tuple<Ss...>, Unk>
    {
        return {tpl_app(static_cast<std::tuple<PFs...> &&>(pfs_), static_cast<Pr &&>(pr),
                        std::make_index_sequence<sizeof...(Fs)>{}),
                static_cast<std::tuple<PSs...> &&>(pss_),
                tpl_app(static_cast<std::tuple<Fs...> &&>(fs_), static_cast<V &&>(v),
                        std::make_index_sequence<sizeof...(Fs)>{}),
                static_cast<std::tuple<Ss...> &&>(ss_), static_cast<Unk &&>(unk_)};
    }
    template <jutil::callable<> V>
    JUTIL_CI auto operator()(const string_view sv, V &&v) &&noexcept(
        noexcept(static_cast<this_type &&> (*this)(strs(sv), static_cast<V &&>(v))))
        -> decltype(static_cast<this_type &&>(*this)(strs(sv), static_cast<V &&>(v)))
    {
        return static_cast<this_type &&>(*this)(strs(sv), static_cast<V &&>(v));
    }
};
} // namespace detail

using detail::strs;

constexpr inline detail::visitor_fbdef default_visitor;

template <jutil::callable<string_view> Unknown = detail::visitor_fbdef>
constexpr auto make_visitor(Unknown &&unk = {})
    -> detail::visitor_maker<std::tuple<>, std::tuple<>, std::tuple<>, std::tuple<>, Unknown>
{
    return {{}, {}, {}, {}, static_cast<Unknown &&>(unk)};
}

JUTIL_CI int visit(int argc, char *argv[], opt_visitor auto &&ov, arg_visitor auto &&av) noexcept(
    noexcept(av(string_view{})) &&noexcept(ov(string_view{}, [] -> arg_ty { return 0; })))
{
#define o_go_visit(...)                                                                            \
    do {                                                                                           \
        if (const auto BOOST_PP_CAT(res, __LINE__) = detail::visit(__VA_ARGS__))                   \
            return BOOST_PP_CAT(res, __LINE__);                                                    \
    } while (0)
    for (auto f = argv + 1, l = argv + argc; f != l; ++f) {
        const string_view opt{*f};
        if (!opt.starts_with('-')) {
            o_go_visit(av, opt);
        } else if (opt == "--") {
            while (++f != l)
                o_go_visit(av, string_view{*f});
            break;
            // } else if (opt == "-help" || opt == "h") {
            // TODO: print help
        } else {
            o_go_visit(ov, opt.substr(1), [&] -> arg_ty {
                if (++f == l) {
                    fprintf(stderr, "argument expected after %s", f[-1]);
                    return 1;
                } else
                    return *f;
            });
        }
    }
    return 0;
#undef o_go_visit
}

} // namespace options

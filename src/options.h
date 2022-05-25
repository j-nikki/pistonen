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

constexpr inline struct help_tag {
} help;

template <class>
struct nstr;
template <std::size_t N>
struct nstr<const char (&)[N]> : std::integral_constant<std::size_t, N - 1> {
};

template <std::size_t N>
JUTIL_CI auto slit2arr(const char (&xs)[N]) noexcept
{
    std::array<char, N - 1> res{};
    for (std::size_t i = N - 1; i--;)
        res[i] = xs[i];
    return res;
}

template <class...>
struct strs_ty;
template <std::size_t... Is, class U, class... Ts>
struct strs_ty<std::index_sequence<Is...>, U, Ts...> {
    std::tuple<Ts...> ss;
    U h;
    JUTIL_CI bool operator()(const string_view sv) const noexcept
    {
        return ((string_view{std::get<Is>(ss)} == sv) || ...);
    }
    template <std::size_t N>
    JUTIL_CI strs_ty<std::index_sequence<Is...>, std::array<char, N - 1>, Ts...>
    operator()(help_tag, const char (&h_)[N]) &&noexcept
    {
        return {std::move(ss), slit2arr(static_cast<const char(&)[N]>(h_))};
    }
};
template <class U, class... Ts>
strs_ty(std::tuple<Ts...> &&, U &&) -> strs_ty<std::index_sequence_for<Ts...>, U, Ts...>;

template <class... Ts>
JUTIL_CI auto strs(Ts &&...xs_) noexcept
{
    return strs_ty{std::tuple{slit2arr(static_cast<Ts &&>(xs_))...}, std::array<char, 0>{}};
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

constexpr inline auto each = []<class... Ts, class F>(const std::tuple<Ts...> &xs, F &&f_) {
    [&]<std::size_t... Is>(std::index_sequence<Is...>, F && f)
    {
        (f(std::get<Is>(xs), std::bool_constant<Is == sizeof...(Ts) - 1>{}), ...);
    }
    (std::index_sequence_for<Ts...>{}, static_cast<F &&>(f_));
};

template <class Vs>
struct visitor_selector {
    Vs vs_;
    string_view name_;
    string_view what_;
    string_view long_;
    JUTIL_CI void operator()(help_tag, auto &b) const
    {
        // TODO: build this at compile-time
        b.append("\033[1mNAME\033[0m\n    ", name_, " - ", what_,
                 "\n\n\033[1mSYNOPSIS\033[0m\n    ", name_);
        each(vs_.pfs_, [&](const auto &pf, auto) {
            b.append(" [");
            each(pf.ss, [&](const auto &s, auto l) {
                b.append("\033[1m-", s, "\033[0m", (l ? "]" : "|"));
            });
        });
        each(vs_.pss_, [&](const auto &ps, auto) {
            b.append(" [");
            each(ps.ss, [&](const auto &s, auto l) {
                b.append("\033[1m-", s, "\033[0m", (l ? " arg]" : "|"));
            });
        });
        if (!long_.empty())
            b.append("\n\n\033[1mDESCRIPTION\033[0m\n    ", long_);
        b.append("\n\n\033[1mOPTIONS\n    --help\033[0m, \033[1m-h\033[0m               Print "
                 "usage information and exit.\n");
        each(vs_.pfs_, [&](const auto &pf, auto) {
            b.append("    \033[1m");
            const auto n0 = b.size();
            each(pf.ss, [&](const auto &s, auto l) { b.append("-", s, (l ? "" : ", ")); });
            auto n = b.size() - n0;
            b.append("\033[0m");
            if (n > 25) {
                b.append('\n');
                n = 0;
            }
            for (; n < 25; ++n)
                b.append(' ');
            b.append(pf.h, '\n');
        });
        each(vs_.pss_, [&](const auto &ps, auto) {
            b.append("    ");
            auto n0 = b.size();
            each(ps.ss, [&](const auto &s, auto l) {
                n0 += 8, b.append("\033[1m-", s, "\033[0m arg", (l ? "" : ", "));
            });
            auto n = b.size() - n0;
            if (n > 25) {
                b.append('\n');
                n = 0;
            }
            for (; n < 25; ++n)
                b.append(' ');
            b.append(ps.h, '\n');
        });
    }
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
    JUTIL_CI auto operator()(const string_view name, const string_view what,
                             const string_view long_ = {}) &&noexcept -> visitor_selector<this_type>
    {
        return {static_cast<this_type &&>(*this), name, what, long_};
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
};
} // namespace detail

using detail::help;
using detail::help_tag;
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
        } else if (opt == "-help" || opt == "h") {
            buffer b;
            ov(help, b);
            printf("%.*s\n", static_cast<int>(b.size()), b.data());
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

#pragma once

#include <boost/variant2.hpp>
#include <ranges>
#include <string_view>

#include "jutil.h"

#include "lmacro_begin.h"

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
//
// helpers
//

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

template <std::size_t N>
JUTIL_CI auto slit2arr(const char (&xs)[N]) noexcept
{
    std::array<char, N - 1> res{};
    for (std::size_t i = N - 1; i--;)
        res[i] = xs[i];
    return res;
}

//
// strs
//

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

//
// help builder, run twice; (1) to find required buffer size, and (2) to populate the buffer
//

constexpr inline auto fxfwd =
    jutil::overload{FREF(slit2arr), []<class T>(T &&x) { return static_cast<T &&>(x); }};
constexpr inline auto fxb = []<class... Ts>(Ts &&...xs) {
    return format::fg(format::bold(std::tuple{fxfwd(static_cast<Ts &&>(xs))...}),
                      format::bright_cyan);
};
constexpr inline auto nbcs = format::maxsz(fxb());
constexpr inline auto fxa  = []<class... Ts>(Ts &&...xs) {
    return format::fg(format::bold(std::tuple{fxfwd(static_cast<Ts &&>(xs))...}),
                       format::bright_green);
};
constexpr inline auto nacs = format::maxsz(fxa());
JUTIL_CI void build_help(auto &&pfs_, auto &&pss_, auto &&name_, auto &&what_, auto &&long_,
                         auto &&f, auto &&g)
{ // Builds a man-page style listing - TODO: simplified terse style
    f(fxb("NAME\n\t"), name_, " - ", what_, fxb("\n\nSYNOPSIS\n\t"), name_);
    jutil::each(pfs_, [&](auto &&pf) {
        f(" ["), jutil::each(pf.ss, L2((f(fxb("-", x), (y ? "]" : "|"))), &));
    });
    jutil::each(pss_, [&](auto &&ps) {
        f(" ["), jutil::each(ps.ss, L2((f(fxb("-", x)), y ? f(fxa(" arg"), "]") : f('|')), &));
    });
    if (!long_.empty()) f(fxb("\n\nDESCRIPTION\n\t"), long_);
    f(fxb("\n\nOPTIONS\n\t--help"), ", ", fxb("-h"),
      "               Print usage information and exit.");
    const auto opts = [&](auto &&ps, auto &&usage) {
        jutil::each(ps, [&](auto &&pf) {
            f("\n\t");
            auto n0 = g();
            jutil::each(pf.ss, [&](auto &&s, auto, auto l) { n0 += usage(s, l); });
            auto n = g() - n0;
            if (n > 25) f("\n\t"), n = 0;
            for (; n < 25; ++n)
                f(' ');
            f(pf.h);
        });
    };
    opts(pfs_, L2((f(fxb("-", x), (y ? "" : ", ")), nacs), &));
    opts(pss_, L2((f(fxb("-", x), fxa(" arg"), (y ? "" : ", ")), nacs + nbcs), &));
}

//
// visitor selection
//

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

template <class Vs, std::size_t NN, std::size_t NW, std::size_t NL>
struct visitor_selector {
    Vs vs_;
    std::array<char, NN> name_;
    std::array<char, NW> what_;
    std::array<char, NL> long_;
    static constexpr auto nhelp = [] {
        std::size_t res = 0;
        build_help(
            decltype(vs_.pfs_){}, decltype(vs_.pss_){}, std::array<char, NN>{},
            std::array<char, NW>{}, std::array<char, NL>{},
            [&]<class... Args>(Args &&...args) {
                res += format::maxsz(static_cast<Args &&>(args)...);
            },
            [&] { return res; });
        return res;
    }();
    std::array<char, nhelp> help = [&] {
        std::array<char, nhelp> res;
        char *it = res.data();
        build_help(
            vs_.pfs_, vs_.pss_, name_, what_, long_,
            [&]<class... Args>(Args &&...args) {
                it = format::format(it, static_cast<Args &&>(args)...);
            },
            [&] { return static_cast<std::size_t>(it - res.data()); });
        return res;
    }();
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

//
// visitor_maker
//

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

    //
    // finalize
    //

    template <std::size_t NN, std::size_t NW, std::size_t NL>
    JUTIL_CI auto operator()(const char (&name)[NN], const char (&what)[NW],
                             const char (&long_)[NL]) &&noexcept
        -> visitor_selector<this_type, NN - 1, NW - 1, NL - 1>
    {
        return {static_cast<this_type &&>(*this), slit2arr(name), slit2arr(what), slit2arr(long_)};
    }
    template <std::size_t NN, std::size_t NW>
    JUTIL_CI auto operator()(const char (&name)[NN], const char (&what)[NW]) &&noexcept
        -> visitor_selector<this_type, NN - 1, NW - 1, 0>
    {
        return {static_cast<this_type &&>(*this), slit2arr(name), slit2arr(what), {}};
    }

    //
    // add command
    //

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

//
// make_visitor: starts visitor making
//

template <jutil::callable<string_view> Unknown = detail::visitor_fbdef>
constexpr auto make_visitor(Unknown &&unk = {})
    -> detail::visitor_maker<std::tuple<>, std::tuple<>, std::tuple<>, std::tuple<>, Unknown>
{
    return {{}, {}, {}, {}, static_cast<Unknown &&>(unk)};
}

//
// visit: invokes visitor with program input
//

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
        } else if (opt == "--help" || opt == "-h") {
            printf("%.*s\n", static_cast<int>(ov.help.size()), ov.help.data());
            return 1;
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

#include "lmacro_end.h"

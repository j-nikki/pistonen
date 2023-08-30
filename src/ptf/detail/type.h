// #pragma once

// #include <boost/variant2.hpp>
// #include <ranges>

// #include "../../jutil.h"

// #include "../../lmacro_begin.h"

// namespace ptf
// {
// namespace sr = std::ranges;
// namespace sv = std::views;
// namespace bv = boost::variant2;

// // https://webassembly.github.io/spec/core/syntax/values.html
// enum valty {
//     // valty
//     i32    = 0x7f,
//     i64    = 0x7e,
//     f32    = 0x7d,
//     f64    = 0x7c,
//     vec    = 0x7b,
//     fnref  = 0x70,
//     extref = 0x6f,

//     fnty   = 0x60,
// };

// struct fnsig {
//     std::vector<void *> params;
//     std::vector<void *> rets;
//     inline fnsig(const std::initializer_list<std::string_view> ps,
//                  const std::initializer_list<std::string_view> rs);
// };

// using type                 = bv::variant<struct fnsig, valty>;

// using type_map             = robin_hood::unordered_node_map<std::string_view, type>;
// using type_ptr             = type_map::value_type *;

// constexpr inline auto toty = sv::transform(L(static_cast<type_ptr>(x)));

// struct symbol_ {
//     type_ptr type;
//     bool mut;
// };

// using sym_map = robin_hood::unordered_node_map<std::string_view, symbol_>;
// using sym_ptr = sym_map::value_type *;

// extern type_map types;
// extern sym_map syms;

// JUTIL_INLINE type_ptr get_type(const std::string_view sv) noexcept
// {
//     const auto it = types.find(sv, robin_hood::is_transparent_tag{});
//     return it == types.end() ? nullptr : &*it;
// }

// inline fnsig::fnsig(const std::initializer_list<std::string_view> ps,
//                     const std::initializer_list<std::string_view> rs)
// {
//     for (const auto t : ps | sv::transform(FREF(get_type)))
//         params.push_back(t);
//     for (const auto t : rs | sv::transform(FREF(get_type)))
//         params.push_back(t);
// }

// } // namespace ptf

// #include "../../lmacro_end.h"

#pragma once

#include <boost/variant2.hpp>
#include <ranges>
#include <robin_hood.h>

#include "../../jutil.h"

#include "../../lmacro_begin.h"

namespace ptf
{
namespace sr = std::ranges;
namespace sv = std::views;
namespace bv = boost::variant2;

// https://webassembly.github.io/spec/core/syntax/values.html
enum valty {
    // valty
    i32    = 0x7f,
    i64    = 0x7e,
    f32    = 0x7d,
    f64    = 0x7c,
    vec    = 0x7b,
    fnref  = 0x70,
    extref = 0x6f,

    fnty   = 0x60,
};

struct fnsig {
    std::vector<void *> params;
    std::vector<void *> rets;
};

using type                 = bv::variant<struct fnsig, valty>;

using type_map             = robin_hood::unordered_node_map<std::string, type>;
using type_ptr             = type_map::value_type *;

constexpr inline auto toty = sv::transform(L(static_cast<type_ptr>(x)));

struct symbol_ {
    type_ptr type;
    bool mut;
};

using sym_map = robin_hood::unordered_node_map<std::string, symbol_>;
using sym_ptr = sym_map::value_type *;

extern type_map types;
extern sym_map syms;
} // namespace ptf

#include "../../lmacro_end.h"

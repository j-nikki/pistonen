#include "ptf/ptf.h"
#include "ptf/detail/type.h"
#include "ptf/detail/writer.h"
#include <boost/iterator/function_output_iterator.hpp>

#include <fstream>
#include <range/v3/all.hpp>
#include <tao/pegtl.hpp>
#include <tao/pegtl/file_input.hpp>

#include "jutil.h"

#include "lmacro_begin.h"

namespace r3 = ranges::v3;
namespace rv = r3::view;

/*
The pistonen template file (PTF) syntax:

PTF file is structured as
```
  import*
  statement*,
```
where
```
  import: "import" *|= "<" fname ">" |"\""" fname "\"" LF
  dname: [a-zA-Z_][a-zA-Z0-9_]+
  fname: dname|bin-op-name|unary-op-name|"()"|"[]"

  definition: dname? :? type? fn-params|ctor-args? = struct-body|fn-body|statement
  struct-body: member-def | struct-body-long*
  struct-body-long: LF child-indent member-def
  member-def: fname? :? type? fn-params|ctor-args? = struct-body|fn-body|statement
  fn-body: statement | fn-body-long*
  fn-body-long: LF child-indent statement
  return-statement: "return" (definition|expression|<dname>) LF
  statement: (definition|expression|<dname>) LF

  expression: literal|dname|fn-call|bin-op|unary-op|expression "[" expression "]"
  literal: false | true | "\"" [^"]* "\"" | (0x)?\d+\.?\d*(e\d+)+ | "`" tmpllit-seq* "`"
  tmpllit-seq: [^$`]*(${expression})?
  fn-call: "await"? dname "(" args,... ")"
  fn-params: "(" name?:?type?,... ")"
  bin-op: expression LF* bin-op-name LF* expression
  bin-op-name: "=="|"!="|[+-%/*|&^<>=]
  unary-op: unary-op-name expression
  unary-op-name: [+-!~]
  type: dname|"type"(dname)|"i64"|"f32"|"f64"|"v128"|type fn-params|type[]|name.
```

Example:
```
import =<f1> # defs under f1
import *"f2" # defs under current ns

=db{} [](x) = (await fetch(`api/${x}`)).json()

map:B[](xs:A[], f:B(A)) = ...f(xs)

<div> ...<p>`${map(db["clients"], []"name")}`
```
*/

using namespace tao::pegtl;
namespace sf = std::filesystem;

//
// utils
//

using indent = size_t;

template <class T = __uint128_t, T Len = 8, T Init = 0>
struct vector {
    T x_ = Init;
    [[nodiscard]] constexpr T peek() const noexcept { return x_ & ((1 << Len) - 1); }
    constexpr void add(const T x) noexcept { CHECK(peek() + x, < (1 << Len)), x_ += x; }
    constexpr void push(const T x) noexcept { grow(), add(x); }
    constexpr void grow() noexcept { x_ <<= Len; }
    constexpr void shrink() noexcept { x_ >>= Len; }
};

sf::path locate_source_file(std::string_view /*name*/)
{
    return {}; // TODO: impl
}

//
// grammar definition
//

/*
f() = () = () = 42
f()()()
*/

struct grammar;
template <class R>
struct action_ : nothing<R> {
};
template <class R>
struct control_ : normal<R> {
};

struct rbound : not_at<identifier_other> {};

//
// spacing
//

// Note: no support for tab, CR, etc.; not needed as modern editors can automatically convert these.
// Also, given significant indentation, it's to the benefit of one's sanity to only allow spaces.
struct xspace : one<' ', '\n'> {}; // only sensible mid-expressions (over lf)
struct pxspace : plus<xspace> {};
struct sxspace : star<xspace> {};
struct space_ : one<' '> {};
struct pspace : plus<space_> {};
struct sspace : star<space_> {};
struct comment : seq<one<'#'>, star<not_one<'\n'>>> {};
struct indent_ : star<one<' '>> {};
struct lf : seq<plus<sspace, opt<comment>, one<'\n'>>, indent_> {};
template <>
struct action_<indent_> {
    static bool apply(const auto &in, indent &idt) noexcept { return in.size() == idt; }
};
template <class R>
struct nest : R {
};
template <class R>
struct control_<nest<R>> : normal<nest<R>> {
    template <apply_mode A, rewind_mode M, template <typename...> class Action,
              template <typename...> class Control>
    static bool match(const auto &in, indent &idt)
    {
        idt += 4;
        DEFER[&] { idt -= 4; };
        return normal<nest<R>>::template match<A, M, Action, Control>(in, idt);
    }
};

//
// import
//

template <bool Relative>
struct import;
template <>
struct import <true> : seq<one<'=', '*'>, one<'"'>, identifier, one<'"'>> {
};
template <>
struct import <false> : seq<one<'=', '*'>, one<'<'>, identifier, one<'>'>> {
};
struct importln : seq<string<'i', 'm', 'p', 'o', 'r', 't'>, pspace,
                      must<sor<import <false>, import <true>>>, lf> {};
template <bool Relative>
struct action_<import <Relative>> {
    static auto apply(const auto &in, indent &)
    {
        file_input fi{Relative ? sf::path{in.input().source()} / in.string_view()
                               : locate_source_file(in.input())};
        indent idt = 0;
        return parse_nested<grammar, action_>(in.input(), fi, idt);
    }
};

struct dname : identifier {};
struct bin_op_name : sor<string<'=', '='>, string<'!', '='>,
                         one<'+', '-', '%', '/', '*', '|', '&', '^', '<', '>', '='>> {};
struct unary_op_name : one<'+', '-', '!'> {};
struct fname : sor<dname, bin_op_name, unary_op_name, string<'(', ')'>, string<'[', ']'>> {};

struct type;
struct fn_params;
struct ctor_args;
struct struct_body;
struct fn_body;

//
// literals
//

struct hexfloat : seq<string<'0', 'x'>, star<xdigit>, one<'.'>, star<xdigit>, opt<one<'f'>>> {};
struct float_ : seq<star<digit>, one<'.'>, star<digit>, opt<one<'f'>>> {};
struct hexint : seq<string<'0', 'x'>, plus<xdigit>, opt<one<'l'>>> {};
struct int_ : seq<plus<digit>, opt<one<'l'>>> {};
struct bool_ : sor<string<'t', 'r', 'u', 'e'>, string<'f', 'a', 'l', 's', 'e'>> {};
struct string_ : seq<one<'"'>, star<sor<string<'\\', '"'>, not_one<'"'>>>, one<'"'>> {};
struct tmplit_seq : seq<star<not_one<'`', '$'>>, string<'$', '{'>, struct expression, one<'}'>> {};
struct tmplit : seq<one<'`'>, star<tmplit_seq>, one<'`'>> {};
struct literal : seq<sor<bool_, hexfloat, hexint, float_, int_>> {};

//
// types
//

struct fn_params : seq<one<'('>, list<struct type, one<','>, space>, one<')'>> {};
struct fn_sig : seq<type, sspace, fn_params> {};
struct type : seq<opt<string<'m', 'u', 't'>, pspace>, dname, opt<sspace, string<'[', ']'>>,
                  opt<sspace, fn_params>> {};

struct expression : sor<literal> {};

struct statement : seq<sor<expression>, lf> {};
// struct definition : seq < opt<dname>, sspace, opt < one<':'>, sspace, type,
//     sor<fn_params, ctor_ars>, sspace, one<'='>,
//     sspace

struct fn_body_long : seq<lf, statement> {};
struct fn_body : sor<statement, nest<star<fn_body_long>>> {};

struct nimport : identifier {};
struct grammar : until<eof, star<importln>, star<statement>> {};

namespace ptf
{
bool parse(const sf::path &source)
{
    file_input fi{source};
    indent idt = 0;
    return parse<grammar, action_, control_>(fi, idt);
}

type_map types{
    {"i32", valty::i32},
    {"i64", valty::i64},
    {"f32", valty::f32},
    {"f64", valty::f64},
};
sym_map syms;

void dump_hex(jutil::sized_input_range auto &&xs, std::output_iterator<char> auto d_f,
              const bool dim = true) noexcept
{
    static constexpr char xds[]{'0', '1', '2', '3', '4', '5', '6', '7',
                                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    const auto put  = L(*d_f++ = x, &);
    const auto putx = L2((put(xds[y / 0x10]), put(xds[y % 0x10]), x == 7 && put(' ')), &);

    const auto asz  = jutil::bsr(CHECK(sr::size(xs), != 0)) / 4;
    std::array<char, sizeof(sr::size(xs)) * 2> abuf;

    jutil::call_n(L0(put(' '), &), asz * 2 + 1);
    jutil::call_with(put, std::string_view{dim ? "\033[2m" : ""});
    jutil::call_n(M0((putx(i, static_cast<uint8_t>(i)), ++i), &, i = 0), 16);
    jutil::call_with(put, std::string_view{dim ? "\033[0m" : ""});

    for (auto &&[i, ys] : xs | rv::chunk(16) | rv::enumerate) {
        jutil::call_with(put, std::string_view{dim ? "\n\033[2m" : "\n"});
        auto aput = M((*b++ = xds[x % 0x10], *b++ = xds[x / 0x10]), &, b = abuf.rbegin());
        jutil::call_n(M0((aput(a % 0x100), a /= 0x100), &, a = i * 16), asz);
        std::copy_n(abuf.begin() + (abuf.size() - asz * 2), asz * 2, d_f);
        jutil::call_with(put, std::string_view{dim ? "\033[0m " : " "});

        for (auto &&[j, y] : ys | rv::enumerate)
            putx(j, static_cast<uint8_t>(y));
    }
}

void test()
{
    writer w;
    types.emplace("asd", fnsig{{&types["i32"]}, {&types["i32"]}});
    w.write_module();
    dump_hex(w.span(), jutil::fn_it{L(putc(x, stdout))});
    putc('\n', stdout);
    std::ofstream ofs{"own/gen.wasm"};
    ofs.write(w.data(), w.size());
    puts("written to gen.wasm");
}
} // namespace ptf

void ptf::writer::reserve(const std::size_t new_cap)
{
    auto new_buf = std::make_unique_for_overwrite<char[]>(new_cap);
    memcpy(new_buf.get(), buf_.get(), sz_);
    buf_.swap(new_buf);
    cap_ = new_cap;
}

#include "lmacro_end.h"

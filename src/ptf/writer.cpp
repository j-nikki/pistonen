// #include "detail/writer.h"

// #include <fstream>
// // #include <range/v3/all.hpp>
// #include <ranges>

// #include "../jutil.h"
// #include "detail/type.h"
// #include "ptf.h"

// #include "../lmacro_begin.h"

// namespace ptf
// {
// namespace sr = std::ranges;
// // namespace r3 = ranges::v3;
// // namespace rv = r3::view;

// void dump_hex(jutil::sized_input_range auto &&xs, std::output_iterator<char> auto d_f,
//               const bool dim = true) noexcept
// {
//     static constexpr char xds[]{'0', '1', '2', '3', '4', '5', '6', '7',
//                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
//     const auto put  = L(*d_f++ = x, &);
//     const auto putx = L2((put(xds[y / 0x10]), put(xds[y % 0x10]), x == 7 && put(' ')), &);

//     const auto asz  = jutil::bsr(CHECK(sr::size(xs), != 0)) / 4;
//     std::array<char, sizeof(sr::size(xs)) * 2> abuf;

//     jutil::call_n(L0(put(' '), &), asz * 2 + 1);
//     sr::for_each(std::string_view{dim ? "\033[2m" : ""}, put);
//     jutil::call_n(M0((putx(i, static_cast<uint8_t>(i)), ++i), &, i = 0), 16);
//     sr::for_each(std::string_view{dim ? "\033[0m" : ""}, put);

//     for (auto &&[i, ys] : xs | sv::chunk(16) | sv::enumerate) {
//         sr::for_each(std::string_view{dim ? "\n\033[2m" : "\n"}, put);
//         auto aput = M((*b++ = xds[x % 0x10], *b++ = xds[x / 0x10]), &, b = abuf.rbegin());
//         jutil::call_n(M0((aput(a % 0x100), a /= 0x100), &, a = i * 16), asz);
//         std::copy_n(abuf.begin() + (abuf.size() - asz * 2), asz * 2, d_f);
//         sr::for_each(std::string_view{dim ? "\033[0m " : " "}, put);

//         for (auto &&[j, y] : ys | sv::enumerate)
//             putx(j, static_cast<uint8_t>(y));
//     }
// }

// void print_type(const auto &t, int idt = 2)
// {
//     const auto &[k, v] = t;
//     printf("%.*s: ", static_cast<int>(k.size()), k.data());
//     const jutil::overload visitor{
//         [&](const valty &v_) { printf("\033[1m%#x\033[0m\n", std::to_underlying(v_)); },
//         [&](const fnsig &f) {
//             printf("\033[1mfn \033[0;2m[%zup %zur]\033[0m\n", f.params.size(), f.rets.size());
//             for (auto &&[ys, c] : {jutil::forward(f.params, 'p'), jutil::forward(f.rets, 'r')}) {
//                 for (auto &&[i, y] :
//                      ys | sv::transform(L(static_cast<type_ptr>(x))) | sv::enumerate) {
//                     jutil::call_n(L0(putc(' ', stdout)), idt);
//                     printf("\033[2m[%c%zd]\033[0m ", c, i);
//                     print_type(*y, idt + 2);
//                 }
//             }
//         }};
//     bv::visit(visitor, v);
// }

// void writer_test()
// {
//     writer w;
//     types.emplace("asd", fnsig{{"i32"}, {"i32"}});
//     w.write_module();
//     sr::for_each(types, FREF(print_type));
//     dump_hex(w.span(), jutil::fn_it{L(putc(x, stdout))});
//     putc('\n', stdout);
//     std::ofstream ofs{"own/gen.wasm"};
//     ofs.write(w.data(), w.size());
//     puts("written to own/gen.wasm");
// }

// void writer::reserve(const std::size_t new_cap)
// {
//     auto new_buf = std::make_unique_for_overwrite<char[]>(new_cap);
//     memcpy(new_buf.get(), buf_.get(), sz_);
//     buf_.swap(new_buf);
//     cap_ = new_cap;
// }
// } // namespace ptf

// #include "../lmacro_end.h"

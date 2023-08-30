#include "message.h"

#include <jutil/alg.h>
#include <jutil/bit.h>
#include <magic_enum.hpp>
#include <string.h>

#include <jutil/lmacro.inl>

constexpr auto phdrws = L(x == ' ' || x == '\t');

namespace j           = jutil;

namespace pnen
{
void headers::grow()
{
    const auto new_cap = cap_ * 2;
    auto new_mem       = std::make_unique_for_overwrite<entry[]>(new_cap);
    std::copy_n(buf_.get(), n_, new_mem.get());
    buf_.reset(new_mem.release());
    cap_ = new_cap;
}

void headers::reserve(char *const kf, char *const kl, char *const vf, char *const vl)
{
    if (n_ == cap_) [[unlikely]]
        grow();
    const auto f = begin();
    const auto l = end();
    l->first.n   = 0;
    const pnen::string k{kf, static_cast<std::size_t>(kl - kf)};
    const auto lb = j::find_if_always_idx(f, l + 1, L(!(k < x.first), k));
    memmove(&f[lb + 1], &f[lb], sizeof(entry) * (n_ - lb));
    ++n_;
    f[lb] = {k, {vf, static_cast<std::size_t>(vl - vf)}};
}

//
// parse_header
//

constexpr auto lccnv =
    L(j::to_unsigned(x - 'A') <= j::to_unsigned('Z' - 'A') ? x + ('a' - 'A') : x);
void parse_header(char *f, char *const l, message &msg)
{
    auto i = std::find(f, l, '\r');
    parse_start(f, i, msg.strt);
    msg.hdrs.clear();
    l[1] = ':'; // sentinel
    while (reinterpret_cast<uintptr_t>(i) < reinterpret_cast<uintptr_t>(l)) {
        f = i + 2;
        const auto colon =
            j::transform_always_until(f, l + 2, f, L(static_cast<char>(lccnv(x))), ':');
        const auto val = j::find_if_always(colon + 1, l + 2, L(!(phdrws(x))));
        i              = j::find_always(val, l + 3, '\r');
        msg.hdrs.reserve(f, colon, val, i);
    }
}
void parse_header(std::span<char> str, message &msg)
{
    parse_header(str.data(), str.data() + str.size(), msg);
}

//
// print_header
//

void print_header(const message &msg)
{
    const auto mtds = magic_enum::enum_name(msg.strt.mtd);
    const auto ver  = magic_enum::enum_name(msg.strt.ver);
    printf("METHOD: %.*s\nTARGET: %.*s\nVERSION: %.*s\n", static_cast<int>(mtds.size()),
           mtds.data(), static_cast<int>(msg.strt.tgt.n), msg.strt.tgt.p,
           static_cast<int>(ver.size()), ver.data());
    for (const auto [a, b] : msg.hdrs)
        printf("  %.*s: %.*s\n", static_cast<int>(a.n), a.p, static_cast<int>(b.n), b.p);
}

//
// parse_start
//

#define MTDSTRCNV(X)                                                                               \
    (static_cast<int64_t>(sizeof(X) < 2 ? 0xff : X[0]) |                                           \
     (static_cast<int64_t>(sizeof(X) < 3 ? 0xff : X[1]) << 8) |                                    \
     (static_cast<int64_t>(sizeof(X) < 4 ? 0xff : X[2]) << 16) |                                   \
     (static_cast<int64_t>(sizeof(X) < 5 ? 0xff : X[3]) << 24) |                                   \
     (static_cast<int64_t>(sizeof(X) < 6 ? 0xff : X[4]) << 32) |                                   \
     (static_cast<int64_t>(sizeof(X) < 7 ? 0xff : X[5]) << 40) |                                   \
     (static_cast<int64_t>(sizeof(X) < 8 ? 0xff : X[6]) << 48) |                                   \
     (static_cast<int64_t>(sizeof(X) < 9 ? 0xff : X[7]) << 56))
#define MTDMAP(X) (~MTDSTRCNV(X))
constexpr int64_t mtds[]{
    MTDMAP("GET"),     MTDMAP("HEAD"),    MTDMAP("POST"),  MTDMAP("PUT"),  MTDMAP("DELETE"),
    MTDMAP("CONNECT"), MTDMAP("OPTIONS"), MTDMAP("TRACE"), MTDMAP("PATH"), 0};
constexpr uint8_t mtdns[]{sizeof("GET") - 1,     sizeof("HEAD") - 1,
                          sizeof("POST") - 1,    sizeof("PUT") - 1,
                          sizeof("DELETE") - 1,  sizeof("CONNECT") - 1,
                          sizeof("OPTIONS") - 1, sizeof("TRACE") - 1,
                          sizeof("PATH") - 1,    0};

void parse_start(char *const f, char *const l, start &s) noexcept
{
    const auto y    = j::loadu<const int64_t>(f);
    const auto mtdi = j::find_if_always_idx(mtds, L(!(x & y), y));
    s.mtd           = static_cast<method>(mtdi);

    const auto tgtf = j::find_if_always(f + mtdns[mtdi], l + 1, L(!phdrws(x)));
    const auto tgtl = j::find_if_snt(tgtf, l + 2, phdrws, ' ');
    s.tgt           = {tgtf, static_cast<std::size_t>(tgtl - tgtf)};

    const auto verf = j::find_if_always(tgtl, l + 1, L(!phdrws(x)));
    const auto v    = j::loadu<const int64_t>(verf);
    if (verf + 8 != l) [[unlikely]]
        s.ver = version::err;
    else if (v == j::loadu<const int64_t>("HTTP/1.0"))
        s.ver = version::http10;
    else if (v == j::loadu<const int64_t>("HTTP/1.1"))
        s.ver = version::http11;
    else [[unlikely]]
        s.ver = version::err;
}
} // namespace pnen

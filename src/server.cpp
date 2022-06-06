#include "server.h"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/transform.hpp>
#include <robin_hood.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "format.h"
#include "jutil.h"
#include "message.h"
#include "pistonen.h"
#include "vocabserv.h"
#include <res.h>

#include "lmacro_begin.h"

#define KEEP_ALIVE_SECS 3

namespace sc = std::chrono;
namespace sf = std::filesystem;
namespace v3 = ranges::v3;
namespace vv = v3::views;
namespace sv = std::ranges::views;

using namespace jutil;

enum mimetype { js, css, html };
static constexpr std::string_view mt_ss[]{"text/javascript", "text/css", "text/html"};

#define STATIC_SV(X, ...)                                                                          \
    ([__VA_ARGS__]() -> const std::string_view & {                                                 \
        static constexpr std::string_view BOOST_PP_CAT(x, __LINE__) = X;                           \
        return BOOST_PP_CAT(x, __LINE__);                                                          \
    })()

namespace pnen::detail
{
void log_ssl_error(SSL *ssl, const char *fname, int err, std::source_location sl)
{
    if (ssl) {
        g_log.print(sl.file_name(), ":", sl.line(), ":", sl.column(), ": fd#", SSL_get_fd(ssl),
                    ": ", fname, "() FAIL, SSL_get_error()=", err);
    } else {
        g_log.print(sl.file_name(), ":", sl.line(), ":", sl.column(), ": ", fname, "()=", err);
    }
    ERR_print_errors_cb(
        +[](const char *str, size_t len, void *) {
            g_log.print("  ", std::string_view{str, len});
            return 0;
        },
        nullptr);
}
} // namespace pnen::detail

[[nodiscard]] JUTIL_INLINE const std::string_view &mimetype_to_string(mimetype mt) noexcept
{
    return mt_ss[CHECK(std::to_underlying(mt), >= 0, <= 2)];
}

[[nodiscard]] JUTIL_INLINE mimetype get_mimetype(const std::string_view uri) noexcept
{
    static constexpr std::string_view exts[]{".js", ".css"};
    return static_cast<mimetype>(find_if_unrl_idx(exts, L(uri.ends_with(x), &)));
}

struct gc_res {
    const std::string_view &type = STATIC_SV(""), &hdr = STATIC_SV("");
};

[[nodiscard]] JUTIL_INLINE gc_res serve_api(const string &uri, buffer &body) noexcept
{
    using namespace std::string_view_literals;
    if (uri == "vocabVer") {
        body.put(std::string_view{"1"});
        return {STATIC_SV("text/plain")};
    }
    if (uri == "vocab") {
        body.put(std::string_view{g_vocab.buf.get(), g_vocab.nbuf});
        return {STATIC_SV("text/plain"), STATIC_SV("content-encoding: gzip\r\n")};
    }
    return {};
}

template <jutil::callable<char *, std::size_t> F>
constexpr format::custom_formatable auto lazywrite(std::size_t n_, F &&f_)
{
    struct R {
        std::size_t n;
        F f;
        constexpr std::size_t size() const noexcept { return n; }
        constexpr char *write(char *p) const noexcept { return f(p, n), p + n; }
    };
    return R{n_, static_cast<F &&>(f_)};
}

namespace hdrs
{
static constexpr std::array static_{
    std::string_view{"content-encoding: gzip\r\n"}, // js
    std::string_view{"content-encoding: gzip\r\n"}, // css
    std::string_view{"content-encoding: gzip\r\n"
                     "Content-Security-Policy: frame-ancestors 'none'\r\n"} // html
};
static constexpr std::array dynamic{
    std::string_view{""}, // js
    std::string_view{""}, // html
    std::string_view{""}  // css
};
} // namespace hdrs

[[nodiscard]] JUTIL_INLINE gc_res get_content(const string &uri, buffer &body)
{
    using namespace std::string_view_literals;
    if (uri.sv().starts_with("/api/")) {
        g_log.print("  serving api request: ", uri.sv());
        return serve_api(uri.substr(5), body);
    }
    const auto mt = get_mimetype(uri);
    if (const auto idx = find_unrl_idx(res::names, uri); idx < res::names.size()) {
        body.put(res::contents[idx]);
        g_log.print("  serving static file: ", uri.sv());
        return {mimetype_to_string(mt), hdrs::static_[std::to_underlying(mt)]};
    }
    const auto fname = uri.sv().substr(1);
    for (const auto &e :
         sf::recursive_directory_iterator{g_wwwroot} |
             sv::filter(L(x.is_regular_file() && x.path().filename() == fname, &))) {
        FILE *f = fopen(e.path().c_str(), "rb");
        DEFER[=] { fclose(f); };
        body.put(lazywrite(e.file_size(), L2(fread(x, 1, y, f), &)));
        // TODO: gzip-encoded contents
        g_log.print("  serving dynamic file: ", uri.sv());
        return {mimetype_to_string(get_mimetype(uri)), hdrs::dynamic[std::to_underlying(mt)]};
    }

    return {};
}

constexpr std::string_view nf1 = "<!DOCTYPE html><meta charset=utf-8><title>Error 404 (Not "
                                 "Found)</title><p><b>404</b> Not Found.<p>The resource <code>",
                           nf2 = "</code> was not found.";

struct escaped {
    escaped(const std::string_view sv) : f{sv.data()}, l{sv.data() + sv.size()} {}
    constexpr JUTIL_INLINE std::size_t size() const noexcept
    {
        return static_cast<std::size_t>(l - f);
    }
    const char *const f, *const l;
};

template <class It>
It escape(const char *f, const char *const l, It d_f) noexcept
{
#define ESC_copystr(S) std::copy_n(S, sizeof(S) - 1, d_f)
    for (; f != l; ++f) {
        switch (const char c = *f) {
        case '<': d_f = ESC_copystr("&lt;"); break;
        case '>': d_f = ESC_copystr("&gt;"); break;
        case '&': d_f = ESC_copystr("&amp;"); break;
        default: *d_f++ = c;
        }
    }
    return d_f;
}

template <>
struct format::formatter<escaped> {
    static char *format(char *d_f, const escaped &e) noexcept { return escape(e.f, e.l, d_f); }
    static std::size_t maxsz(const escaped &e) noexcept { return e.size() * 5; }
};

void print_vec_u8(const auto &v_, const char *name)
{
    uint8_t xs[sizeof(v_)];
    jutil::overload{
        [&](const __m64 &v) { memcpy(xs, &v, sizeof(v)); },
        [&](const __m128i &v) { _mm_storeu_si128(reinterpret_cast<__m128i *>(xs), v); } //
    }(v_);
    printf("\033[2m%s#\033[0;1m", name);
    for (int i = sizeof(v_); i--;)
        printf(" %2x ", xs[i]);
    printf("\n\033[0;2m%s\033[0m ", name);
    for (int i = sizeof(v_); i--;)
        printf("%3u ", xs[i]);
    printf("\n");
}

//
// base64
//

namespace detail
{
template <char A, char B, char C, char D, char E, char F, char G>
const auto b64iset = _mm_set_epi8(static_cast<char>(A + 128), static_cast<char>(B + 128),
                                  static_cast<char>(C + 128), static_cast<char>(D + 128),
                                  static_cast<char>(E + 128), static_cast<char>(F + 128),
                                  static_cast<char>(G + 128), static_cast<char>(A + 128),
                                  static_cast<char>(B + 128), static_cast<char>(C + 128),
                                  static_cast<char>(D + 128), static_cast<char>(E + 128),
                                  static_cast<char>(F + 128), static_cast<char>(G + 128), 0, 0);

template <bool Rs2>
JUTIL_INLINE decltype(auto) b64_idx_set(const auto &s) noexcept
{
    if constexpr (Rs2)
        return _mm_bsrli_si128(s, 2);
    else
        return s;
}

template <bool Rs2>
JUTIL_INLINE auto b64_cidx(const auto cs) noexcept
{
    auto &&ssub      = b64_idx_set<Rs2>(b64iset<'A', 'a', '0', '+', '/', '-', '_'>);
    auto &&slt       = b64_idx_set<Rs2>(b64iset<26, 26, 10, 1, 1, 1, 1>);
    auto &&sblend    = b64_idx_set<Rs2>(b64iset<0, -26, -52, -62, -63, -62, -63>);
    const auto xs    = _mm_sub_epi8(cs, ssub);
    const auto lt    = _mm_cmplt_epi8(xs, slt);
    const auto blend = _mm_blendv_epi8(xs, sblend, lt);
    return _mm_sub_epi8(xs, blend);
}

char *b64_decode_impl(const std::size_t n, const char *it, char *dit) noexcept
{
    CHECK(n % 4, == 0);
    for (const auto l = it + n; it != l; it += 4, dit += 3) {
        auto xs = _mm_set1_epi32(jutil::loadu<int32_t>(it));
        xs      = _mm_or_si128(b64_cidx<false>(xs), b64_cidx<true>(xs));
        xs      = _mm_maddubs_epi16(xs, _mm_set1_epi16(0x0140));
        xs      = _mm_hadd_epi32(xs, xs);
        xs      = _mm_hadd_epi32(xs, xs);
        xs      = _mm_mullo_epi16(xs, _mm_set1_epi32(0x0010'0001));
        xs      = _mm_srli_epi64(xs, 20);
        xs      = _mm_shuffle_epi8(xs, _mm_set1_epi32(0x000102));
        _mm_storeu_si32(&*dit, xs);
    }
    return dit;
}
} // namespace detail

template <class I, class O>
struct b64_decode_result {
    [[no_unique_address]] sr::iterator_t<I> in;
    [[no_unique_address]] sr::iterator_t<O> out;
};
template <jutil::sized_contiguous_range I, jutil::sized_contiguous_range O>
JUTIL_INLINE b64_decode_result<I, O> b64_decode(I &&i, O &&o) noexcept
{
    const auto n   = std::min(sr::size(i) * 4, sr::size(o) * 3 - 3) / 16 * 4;
    const auto it  = sr::begin(i);
    const auto dit = sr::begin(o);
    return {sr::iterator_t<I>{it + n},
            sr::iterator_t<O>{::detail::b64_decode_impl(n, &*it, &*dit)}};
}

//
// authentication
//

// TODO: remove creds from source code
robin_hood::unordered_flat_map<std::string_view, std::string_view> g_creds{{"admin", "admin123"}};

bool check_creds(const std::string_view uname, const std::string_view pass) noexcept
{
    const auto it = g_creds.find(uname);
    const auto ok = it != g_creds.end() && it->second == pass;
    g_log.print("  checking creds: UNAME=", uname, "; PASS=", pass, "; OK=", ok);
    return ok;
}

bool check_auth(const std::string_view sv) noexcept
{
    if (sv.starts_with("Basic ")) {
        char buf[256];
        auto [_, dit]  = b64_decode(sv.substr(6), buf);
        const auto col = sr::find(buf, dit, ':');
        while (dit != col && !dit[-1])
            --dit;
        return check_creds({buf, col}, {col + 1, dit});
    }

    // TODO: support more auth types
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication

    return false;
}

// example: check_auth("Basic dXNlcm5hbWU6cGFzc3dvcmQ="); // checks username:password

//
// request serving
//

//! @brief Writes a response message serving a given request message
//! @param rq Request message to serve
//! @param rs Response message for given request
void serve(const message &rq, buffer &rs, buffer &body)
{
    if (rq.strt.mtd == method::err) goto badreq;
    if (rq.strt.ver == version::err) goto badver;

    if (const auto auth = rq.hdrs.get("Authorization", ""); !check_auth(auth)) {
        rs.put("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic\r\n\r\n");
        g_log.print("  401 Unauthorized");
        return;
    }

#ifndef NDEBUG
    if (rq.strt.tgt.sv() == "/kill") exit(0);
#endif

    g_log.print("serving request: ", std::to_underlying(rq.strt.mtd), " ", rq.strt.tgt);
    switch (rq.strt.mtd) {
    case method::GET: {
        if (const auto [type, hdr] = get_content(rq.strt.tgt, body); !type.empty()) {
            g_log.print("  200 OK");
            rs.put("HTTP/1.1 200 OK\r\nconnection: keep-alive\r\ncontent-type: ", type,
                   "; charset=UTF-8\r\ndate: ", format::hdr_time{}, //
                   "\r\ncontent-length: ", body.size(),
                   "\r\nkeep-alive: timeout=" BOOST_PP_STRINGIZE(KEEP_ALIVE_SECS), //
                       "\r\n", hdr,      //
                       "\r\n", std::string_view{body.data(), body.size()});
        } else {
            g_log.print("  404 Not Found");
            const escaped res = rq.strt.tgt.sv().substr(0, 100);
            rs.put("HTTP/1.1 404 Not Found\r\n"
                   "content-type: text/html; charset=UTF-8\r\n"
                   "content-length:",
                   nf1.size() + nf2.size() + res.size(), //
                   "\r\ndate: ", format::hdr_time{},     //
                   "\r\n\r\n", nf1, res, nf2);
        }
        return;
    }
    default:;
    }
badreq:
    g_log.print("  400 Bad Request");
    rs.put("400 Bad Request\r\n\r\n\r\n");
    return;
badver:
    g_log.print("  505 HTTP Version Not Supported");
    rs.put("505 HTTP Version Not Supported\r\n\r\n\r\n");
}

DBGSTMNT(static int ncon = 0;)

pnen::task handle_connection(pnen::socket s)
{
    DBGEXPR(const int id_ = ncon++);
    DBGEXPR(printf("con#%d: accepted\n", id_));
    DBGEXPR(DEFER[=] { printf("con#%d: write end\n", id_); });

    // Read into buffer
    static constexpr size_t nbuf = 8 * 1024 * 1024;
    char buf[nbuf];
    message rq;
    FOR_CO_AWAIT (b, rs, s.read(buf, nbuf)) {
        const auto [it, _] = sr::search(b, std::string_view{"\r\n\r\n"});
        if (it == b.end()) {
            if (rs.nbufspn == 0) {
                FOR_CO_AWAIT (s.write("431 Request Header Fields Too Large\r\n\r\n"))
                    ;
                co_return;
            }
        } else { // end of header (CRLFCRLF)
            parse_header(buf, &*it, rq);
            break;
        }
    } else
        co_return;

    // Handle request & build response
    DBGEXPR(printf("vvv con#%d: received message with the header:\n", id_));
    DBGEXPR(print_header(rq));
    DBGEXPR(printf("^^^\n"));
    // TODO: read rq body
    // determining message length (after CRLFCRLF):
    // https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
    buffer rs;
    buffer rs_body;
    serve(rq, rs, rs_body);

    // Write response
    FOR_CO_AWAIT (s.write(rs.data(), rs.size()))
        ;
}

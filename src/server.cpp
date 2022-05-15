#include <charconv>
#include <chrono>
#include <filesystem>
#include <memory>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/transform.hpp>
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

template <auto X>
[[nodiscard]] JUTIL_INLINE constexpr auto &as_static() noexcept
{
    return X;
}

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

static constexpr std::string_view b64chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
bool check_auth(std::string_view sv)
{
    if (!sv.starts_with("Basic "))
        return true;
        
    char buf[256], *dit = buf;
    // ICEs:
    // sv | vv::transform(L(b64chars.find(x)))
    //    | vv::chunk(4)
    //    | vv::transform(L(static_cast<char>((x[0] << 192) | (x[1] << 128) | (x[2] << 64) | x[3])))
    for (auto i = 6uz; i + 4 <= sv.size(); i += 4) {
        const auto x = sv.data() + i;
        const auto y = static_cast<char>((x[0] << 192) | (x[1] << 128) | (x[2] << 64) | x[3]);
        *dit++       = y;
    }
    g_log.print("  got auth: ", std::string_view{buf, static_cast<std::size_t>(dit - buf)});
    return false;
}

//! @brief Writes a response message serving a given request message
//! @param rq Request message to serve
//! @param rs Response message for given request
void serve(const message &rq, buffer &rs, buffer &body)
{
    if (rq.strt.mtd == method::err)
        goto badreq;
    if (rq.strt.ver == version::err)
        goto badver;

    // if (const auto auth = rq.hdrs.get("Authorization", ""); !check_auth(auth)) {
    //     rs.put("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic\r\n\r\n");
    //     g_log.print("  401 Unauthorized");
    //     return;
    // }

#ifndef NDEBUG
    if (rq.strt.tgt.sv() == "/kill")
        exit(0);
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

    // Read into buffer
    static constexpr size_t nbuf = 8 * 1024 * 1024;
    char buf[nbuf];
    message rq;
    FOR_CO_AWAIT (b, rs, s.read(buf, nbuf)) {
        const auto [it, _] = sr::search(b, std::string_view{"\r\n\r\n"});
        if (it == b.end()) {
            if (rs.nbufspn == 0) {
                s.write("431 Request Header Fields Too Large\r\n\r\n");
                co_return;
            }
        } else { // end of header (CRLFCRLF)
            parse_header(buf, &*it, rq);
            break;
        }
    }

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
    CHECK(s.write(rs.data(), rs.size()), != -1);

    DBGEXPR(printf("con#%d: write end\n", id_));
}

void run_server(const int port)
{
    const pnen::run_server_options o{.hostport = static_cast<uint16_t>(port),
                                     .timeout  = {.tv_sec = KEEP_ALIVE_SECS}};
    pnen::run_server(o, handle_connection);
}

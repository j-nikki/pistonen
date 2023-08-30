#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <jutil/core.h>

#include "string.h"

#include <jutil/lmacro.inl>

namespace sr = std::ranges;

namespace pnen
{

//
// STARTLINE
//

enum class method { GET, HEAD, POST, PUT, DELETE_, CONNECT, OPTIONS, TRACE, PATH, err };
enum class version { http10, http11, err };
struct start {
    pnen::string tgt;
    version ver;
    method mtd;
};

void parse_start(char *const f, char *const l, start &s) noexcept;

//
// HEADERS
//

struct headers {
    struct entry {
        pnen::string first, second;
    };
    static constexpr auto defcap = 64uz;

    //
    // SPAN
    //
    [[nodiscard]] JUTIL_INLINE const entry *begin() const noexcept { return buf_.get(); }
    [[nodiscard]] JUTIL_INLINE const entry *end() const noexcept { return buf_.get() + n_; }
    [[nodiscard]] JUTIL_INLINE entry *begin() noexcept { return buf_.get(); }
    [[nodiscard]] JUTIL_INLINE entry *end() noexcept { return buf_.get() + n_; }

    //
    // MODIFICATION
    //
    constexpr JUTIL_INLINE void clear() noexcept { n_ = 0; }
    void grow();
    void reserve(char *const kf, char *const kl, char *const vf, char *const vl);

    //
    // KEY GET
    //
    [[nodiscard]] JUTIL_INLINE const pnen::string *get(const std::string_view keyuc) const
    {
        const auto it = sr::find(begin(), end(), keyuc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? nullptr : &it->second;
    }
    [[nodiscard]] JUTIL_INLINE pnen::string *get(const std::string_view keyuc)
    {
        const auto it = sr::find(begin(), end(), keyuc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? nullptr : &it->second;
    }
    [[nodiscard]] JUTIL_INLINE const pnen::string get(std::string_view keylc,
                                                      pnen::string def) const noexcept
    {
        const auto it = sr::find(begin(), end(), keylc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? def : it->second;
    }
    [[nodiscard]] JUTIL_INLINE pnen::string get(std::string_view keylc, pnen::string def) noexcept
    {
        const auto it = sr::find(begin(), end(), keylc, L(std::string_view(x.first.p, x.first.n)));
        return (it == end()) ? def : it->second;
    }

    std::unique_ptr<entry[]> buf_ = std::make_unique_for_overwrite<entry[]>(defcap);
    std::size_t n_ = 0uz, cap_ = defcap;
};
template <jutil::string S, jutil::string D>
[[nodiscard]] JUTIL_INLINE pnen::string get(headers &h) noexcept
{
    static constexpr auto lc = [] {
        std::array<char, S.size()> res;
        sr::transform(
            S, res.begin(),
            L(jutil::to_unsigned(x - 'A') <= jutil::to_unsigned('Z' - 'A') ? x + ('a' - 'A') : x));
        return res;
    }();
    static constinit auto def = D; // jutil::append<'\0'>(D);
    return h.get({lc.data(), lc.size()}, {def.data(), def.size()});
}
template <jutil::string S>
[[nodiscard]] JUTIL_INLINE pnen::string *get(headers &h) noexcept
{
    static constexpr auto lc = [] {
        std::array<char, S.size()> res;
        sr::transform(
            S, res.begin(),
            L(jutil::to_unsigned(x - 'A') <= jutil::to_unsigned('Z' - 'A') ? x + ('a' - 'A') : x));
        return res;
    }();
    return h.get({lc.data(), lc.size()});
}

//! @brief Parses given HTTP message header; [f:l) must be writable; l must point to CRLFCR and be writable
//! @param f Pointer to the beginning of the message
//! @param l Pointer to the end of the message
//! @param msg Object to represent parsed message
void parse_header(char *f, char *const l, struct message &msg);
void print_header(const struct message &msg);

//
// MESSAGE
//

struct message {
    start strt;
    headers hdrs;
    pnen::string body;
};
} // namespace pnen

#include <jutil/lmacro.inl>

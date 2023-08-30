#pragma once

#include <jutil/core.h>
#include <memory>
#include <string>

#include "format.h"

#include <jutil/lmacro.inl>

namespace pnen
{
template <class ChTy, std::size_t DefCap>
struct buffer_state {
    std::unique_ptr<ChTy[]> buf = std::make_unique_for_overwrite<ChTy[]>(DefCap);
    std::size_t n = 0, cap = DefCap;
};
template <class ChTy, std::size_t DefCap, bool View = false>
struct basic_buffer {
    //
    // DATA RETRIEVAL
    //
    [[nodiscard]] constexpr JUTIL_INLINE std::size_t size() const noexcept { return s_.n; }
    [[nodiscard]] JUTIL_INLINE const ChTy *data() const noexcept { return s_.buf.get(); }
    [[nodiscard]] JUTIL_INLINE const ChTy *begin() const noexcept { return s_.buf.get(); }
    [[nodiscard]] JUTIL_INLINE const ChTy *end() const noexcept { return s_.buf.get() + s_.n; }
    [[nodiscard]] JUTIL_INLINE ChTy *data() noexcept { return s_.buf.get(); }
    [[nodiscard]] JUTIL_INLINE ChTy *begin() noexcept { return s_.buf.get(); }
    [[nodiscard]] JUTIL_INLINE ChTy *end() noexcept { return s_.buf.get() + s_.n; }
    [[nodiscard]] JUTIL_INLINE ChTy &operator[](const std::size_t i) noexcept { return s_.buf[i]; }
    [[nodiscard]] JUTIL_INLINE const ChTy &operator[](const std::size_t i) const noexcept
    {
        return s_.buf[i];
    }
    [[nodiscard]] JUTIL_INLINE basic_buffer<ChTy, DefCap, true> view() const noexcept
        requires(!View)
    {
        return {s_};
    }
    [[nodiscard]] JUTIL_INLINE bool empty() const noexcept { return s_.n == 0; }

    //
    // MODIFICATION
    //
    constexpr JUTIL_INLINE void clear() noexcept { s_.n = 0; }
    template <bool Copy = true>
    void grow(std::size_t n);
    template <bool Copy = true>
    void reserve(std::size_t n)
    {
        if (n > s_.cap) grow<Copy>(n);
    }
    JUTIL_INLINE void shrink(const std::size_t n) noexcept { s_.n -= n; }

  private:
    template <bool Append = false, class... Args>
    JUTIL_INLINE void Put(auto sz, auto fput, Args &&...args)
    {
        const auto base  = Append ? s_.n : 0;
        const auto new_n = base + sz;
        if (new_n > s_.cap) [[unlikely]]
            CHECK(!View), grow<Append>(new_n);
        s_.n = static_cast<std::size_t>(fput(&s_.buf[base], static_cast<Args &&>(args)...) -
                                        &s_.buf[0]);
    }

  public:
    template <bool Append = false, class... Args>
    JUTIL_INLINE std::size_t put(Args &&...args)
    {
        const auto sz = format::maxsz(args...);
        Put<Append>(sz, F(format::format), args...);
        return s_.n;
    }
    template <class... Args>
    JUTIL_INLINE std::size_t append(Args &&...args)
    {
        return put<true>(static_cast<Args &&>(args)...);
    }

  private:
    std::conditional_t<View, buffer_state<ChTy, DefCap> &, buffer_state<ChTy, DefCap>> s_;
};
using buffer = basic_buffer<char, 4096>;
} // namespace pnen

#include <jutil/lmacro.inl>

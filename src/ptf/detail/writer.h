#pragma once

#include "../../jutil.h"
#include <string.h>

#include "../../lmacro_begin.h"

namespace ptf
{
namespace sr = std::ranges;
struct writer {
    static constexpr auto sid_custom     = 0;
    static constexpr auto sid_type       = 1;
    static constexpr auto sid_import     = 2;
    static constexpr auto sid_function   = 3;
    static constexpr auto sid_table      = 4;
    static constexpr auto sid_memory     = 5;
    static constexpr auto sid_global     = 6;
    static constexpr auto sid_export     = 7;
    static constexpr auto sid_start      = 8;
    static constexpr auto sid_element    = 9;
    static constexpr auto sid_code       = 10;
    static constexpr auto sid_data       = 11;
    static constexpr auto sid_data_count = 12;
    static constexpr auto defcap         = 4096;
    void reserve(const std::size_t new_cap);
    JUTIL_INLINE void push(const auto x)
    {
        const auto new_sz = sz_ + sizeof(x);
        if (new_sz > cap_) [[unlikely]]
            reserve(2uz << jutil::intrin::bsr(new_sz));
        jutil::storeu(&buf_[sz_], x);
        sz_ = new_sz;
    }
    template <std::integral T>
    JUTIL_INLINE void write_impl(const T x,
                                 jutil::callable<T> auto &&pusher) noexcept(noexcept(pusher(x)))
    {
        // TODO: trim negative nums
        auto u = jutil::to_unsigned(x);
        for (; u > 0x7f; u >>= 7)
            pusher(static_cast<uint8_t>((u & 0x7f) | 0x80));
        pusher(static_cast<uint8_t>(u));
    }
    JUTIL_INLINE void write_impl(const std::integral auto x_) { write_impl(x_, L(push(x), &)); }
    template <sr::input_range R>
    JUTIL_INLINE void write_impl(R &&xs_)
    {
        write_sized(
            [&](R &&xs) {
                for (auto &&x : xs)
                    write(static_cast<decltype(x) &&>(x));
            },
            static_cast<R &&>(xs_));
    }
    template <jutil::sized_input_range R>
    JUTIL_INLINE void write_impl(R &&xs)
    {
        write(sr::size(xs));
        for (auto &&x : xs)
            write(static_cast<decltype(x) &&>(x));
    }
    inline void write_impl(const type &x)
    {
        const jutil::overload visitor{[&](valty y) { write(std::to_underlying(y)); },
                                      [&](const fnsig &y) {
                                          write(valty::fnty);
                                          write(toty(y.params));
                                          write(toty(y.rets));
                                      }};
        bv::visit(visitor, x);
    }
    inline void write_impl(const type_map::value_type &x) { write(x.second); }
    JUTIL_INLINE void write_impl(const type_ptr tp) { write(tp->second); }
    template <class... Ts>
    void write(Ts &&...xs)
    {
        (write_impl(static_cast<Ts &&>(xs)), ...);
    }
    template <class... Args>
    void write_sized(auto &&writefn, Args &&...args)
    {
        const auto szi = sz_;
        sz_ += 4;
        const auto start = sz_;
        writefn(static_cast<Args &&>(args)...);
        const auto sz = static_cast<uint32_t>(sz_ - start);
#ifndef NDEBUG
        // While redundant, trimming helps with debugging.
        auto i = szi;
        write_impl(sz, L(buf_[i++] = x, &));
        memmove(&buf_[i], &buf_[start], sz);
        sz_ = i + sz;
#else
        buf_[szi + 0] = static_cast<char>(0x80 | (sz & 0x7f));
        buf_[szi + 1] = static_cast<char>(0x80 | ((sz >> 7) & 0x7f));
        buf_[szi + 2] = static_cast<char>(0x80 | ((sz >> 14) & 0x7f));
        buf_[szi + 3] = static_cast<char>(CHECK(sz >> 21, < 0x80));
#endif
    }
    template <class F>
    void write_section(const int32_t section_id, F &&writefn)
    {
        write(section_id);
        write_sized(static_cast<F &&>(writefn));
    }
    void write_start() { push(0x00000001'6d736100u); }
    void write_type_section()
    {
        write_section(sid_type, [&] {
            write(types | sv::filter(L(!bv::holds_alternative<valty>(x.second))));
        });
    }
    void write_export_section()
    {
        // https://webassembly.github.io/spec/core/binary/modules.html#binary-exportdesc
        // 1. writing logic
        // 2. language parser
    }
    void write_vecty() { write(0x7b); }
    void write_refty()
    {
        write(0x70); // func
        write(0x6f); // extern
    }
    void write_name()
    {
        // vector of bytes
    }
    void write_exports() { write(7); }

  public:
    void write_module()
    {
        write_start();
        write_type_section();
    }
    JUTIL_INLINE std::span<const char> span() const noexcept { return {buf_.get(), sz_}; }
    JUTIL_INLINE std::span<char> span() noexcept { return {buf_.get(), sz_}; }
    JUTIL_INLINE std::size_t size() const noexcept { return sz_; }
    JUTIL_INLINE const char *data() const noexcept { return buf_.get(); }
    JUTIL_INLINE char *data() noexcept { return buf_.get(); }

  private:
    std::unique_ptr<char[]> buf_ = std::make_unique_for_overwrite<char[]>(defcap);
    std::size_t cap_             = defcap;
    std::size_t sz_              = 0;
};
} // namespace ptf

#include "../../lmacro_end.h"

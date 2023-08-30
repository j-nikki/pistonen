#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>

#include "buffer.h"
#include "format.h"

#include <jutil/lmacro.inl>

namespace detail
{
namespace sc = std::chrono;
namespace sr = std::ranges;
namespace sv = sr::views;

using namespace format::fx;

using clock = sc::steady_clock;
constexpr std::tuple cols{bright_green, bright_cyan, bright_yellow, bright_red};
struct res {
    int64_t n;
    const char *s;
};
constexpr std::array rs{res(1e0, "ns "), res(1e3, "µs "),  res(1e6, "ms "),
                        res(1e9, "s  "), res(60e9, "m  "), res(3600e9, "h  ")};
struct logger {
    enum class level {
        debug,
        info,
        warn,
        error,
    };
    enum where {
        beg  = 0b01,
        end  = 0b10,
        line = 0b11,
        mid  = 0b00,
    };

#ifndef NDEBUG
    static constexpr inline auto default_level = level::debug;
#else
    static constexpr inline auto default_level = level::info;
#endif

    bool init(const std::string_view dir, const level lvl = default_level);
    void init(FILE *const f, const level lvl = default_level);

  private:
    template <where W, level L, class... Args>
    JUTIL_INLINE std::size_t Print(Args &&...args_)
    {
        if (L != level::error && static_cast<int>(lvl) > static_cast<int>(L)) return 0;
        const auto time = sc::duration_cast<sc::nanoseconds>(clock::now() - start).count();
        const auto it   = sr::lower_bound(rs | sv::drop(2), time, sr::less{}, L(x.n));
        [[maybe_unused]] const auto pre =
            format::fg(std::tuple{format::width<3>(time / it[-1].n), it[-1].s,
                                  format::width<3>(time % it[-1].n / it[-2].n), it[-2].s},
                       std::get<static_cast<int>(L)>(cols));
        std::scoped_lock lk{mtx};
        const auto args = std::tuple_cat(JUTIL_IF(W & beg, std::tuple{pre}, std::tuple{}),
                                         std::forward_as_tuple(static_cast<Args &&>(args_)...),
                                         JUTIL_IF(W & end, std::tuple{"\n"}, std::tuple{}));
        return fwrite(buf.data(), sizeof(char), buf.put(args), file);
    }

  public:
    // clang-format off
    template <where Where = line, class... Args>
    JUTIL_INLINE auto flood([[maybe_unused]] Args &&...args)
#ifndef NDEBUG
    { return Print<Where, level::debug>(static_cast<Args &&>(args)...); }
#else
    noexcept { return 0uz; }
#endif
    // clang-format on
    template <where Where = line, class... Args>
    JUTIL_INLINE auto debug(Args &&...args)
    {
        return Print<Where, level::debug>(static_cast<Args &&>(args)...);
    }
    template <where Where = line, class... Args>
    JUTIL_INLINE auto error(Args &&...args)
    {
        return Print<Where, level::error>(static_cast<Args &&>(args)...);
    }
    template <where Where = line, class... Args>
    JUTIL_INLINE auto info(Args &&...args)
    {
        return Print<Where, level::info>(static_cast<Args &&>(args)...);
    }
    template <where Where = line, class... Args>
    JUTIL_INLINE auto warn(Args &&...args)
    {
        return Print<Where, level::warn>(static_cast<Args &&>(args)...);
    }
    ~logger()
    {
        if (file != stdout) fclose(file);
    }
    pnen::buffer buf;
    std::mutex mtx;
    clock::time_point start;
    FILE *file;
    level lvl;
};
} // namespace detail
using detail::logger;

#ifndef LOG_NO_GLOG
extern logger g_log;
#endif

#include <jutil/lmacro.inl>
